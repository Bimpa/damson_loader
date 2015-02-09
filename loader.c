#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "loader.h"
#include "spiNN_runtime.h"


/* Routing table link direction bits */
#define LINK_EAST			1
#define LINK_NORTH_EAST		1<<1
#define LINK_NORTH			1<<2
#define LINK_WEST			1<<3
#define LINK_SOUTH_WEST		1<<4
#define LINK_SOUTH			1<<5
#define NUM_LINKS			6


/*
 * Structure to hold a a single routing table entry
 */
typedef struct {
	unsigned int key; 	//damson source node id
	unsigned int route; //the route bits, [0-5]=links, [6-23]=cores
}RoutingEntry;

/*
 * Holds the routing table entries for a single chip
 */
typedef struct {
	uint rt_count;
	RoutingEntry rt[MAX_ROUTING_TABLE_ENTRIES];
} ChipConfig;

/*
 * Structure to hold a linked list of node mappings
 */
typedef struct NodeMapItemList NodeMapItemList;
struct NodeMapItemList
{
	NodeMapItem node_map_item;
	NodeMapItemList* next;
};

/*
 * Structure to hold node mappings from the mapping file
 */
typedef struct
{
	unsigned int  damson_node_id;
	int 		  spinnaker_id;	 			// chip x << chip y << core id
	unsigned int  num_logs;
	LoaderLogItem *logs;
	unsigned int  num_snapshots;
	LoaderLogItem *snapshots;
}HardwareMapping;


void 				InitLogFiles(HardwareMapping map);
void 				CloseLogFiles(HardwareMapping map);
void 				OutputLogEntry(HardwareMapping map, unsigned int handle, unsigned int log_items, unsigned int *log_values);

unsigned int 		Hash(unsigned int n, unsigned int size);
void 				AddMapping(HardwareMapping mapping);
void 				AddReverseMapping(HardwareMapping mapping);
HardwareMapping 	GetMapping(unsigned int node_id);
HardwareMapping 	GetReverseMapping(unsigned int spinnaker_id);
unsigned int		NextPower2(unsigned int hash_list_size);		//must be a power of 2

SpiNN_address 		GetSpiNNAddress(unsigned int spinnaker_id);
void 				BuildDeviceIntVector(InterruptVector *int_hash, InterruptVector *intv, unsigned int intvsize);

void 				HandleDebugMessage(SpiNN_address address, char* message);

void 				Route(unsigned int src_id, unsigned int dst_id);
void 				createRoutingEntry(unsigned int chip_index, unsigned int src_id, unsigned int route);

void 				Damson_fprintf(FILE *stream, char *fmt, ...);


char 					spinnaker_ip[128];
unsigned int			spinnaker_layout_width = 0;
unsigned int			spinnaker_layout_height = 0;
unsigned int			spinnaker_chips = 0;
unsigned int			MappingHashSize = 0;
HardwareMapping*		MappingHash = NULL;
HardwareMapping*		ReverseMappingHash = NULL;


ChipConfig				*chips = NULL;
unsigned int 			*core_map;
int						spinnaker_running = 0;
NodeMapItemList			*node_map_start = NULL;
unsigned int			node_count = 0;
FILE 					*spinnaker_config_file = NULL;


void InitLoader(){

	spinnaker_config_file = fopen ("spinnaker.ini","r");

	if (!spinnaker_config_file){
		//error (to be replaced with damson error function for safe shutdown)
		printf("Error: SpiNNaker config file 'spinnaker.ini' does not exist\n");
		exit(0);
	}

	//get the spinnaker configuration
    if (fscanf(spinnaker_config_file, "%s %u %u\n", spinnaker_ip, &spinnaker_layout_width, &spinnaker_layout_height) != 3) {
		//error (to be replaced with damson error function for safe shutdown)
		printf("Error: SpiNNaker config file does not contain a SpiNNaker Configuration in the format 'ip_address layout_width layout_height'\n");
		exit(0);
    }
    spinnaker_chips = spinnaker_layout_width*spinnaker_layout_height;
    chips = (ChipConfig*)malloc(spinnaker_chips*sizeof(ChipConfig));
    core_map = (unsigned int*)malloc(spinnaker_chips*sizeof(unsigned int));

    memset(chips, 0, spinnaker_chips*sizeof(ChipConfig));
    memset(core_map, 0, spinnaker_chips*sizeof(unsigned int));

    fclose(spinnaker_config_file);

    //init the spinnaker board
    if (spiNN_init(spinnaker_ip, spinnaker_layout_width, spinnaker_layout_height) == SPINN_FAILURE){
		printf("Error: Failed to Initialise SpiNNaker hardware\n");
		exit(0);
	}

    sleep(1); //WTF??

    //init debug output
    spinnaker_running = 1;
    spiNN_debug_message_callback(&HandleDebugMessage);
}

void ExitLoader()
{
	free(MappingHash);
	free(ReverseMappingHash);
	free(chips);
	free(core_map);
	spiNN_exit();
}

/**
 * allocated space for logs and snapshots passed to NodeMapItem
 */
void AddNodeMapItem(NodeMapItem *map)
{
	NodeMapItemList *l;
	l = (NodeMapItemList*)malloc(sizeof(NodeMapItemList));
	//clone node_map_item
	l->node_map_item.damson_node_id = map->damson_node_id;
	l->node_map_item.num_interrupts = map->num_interrupts;
	l->node_map_item.interrupts = map->interrupts;
	l->node_map_item.num_logs = map->num_logs;
	l->node_map_item.logs = map->logs;
	l->node_map_item.num_snapshots = map->num_snapshots;
	l->node_map_item.snapshots = map->snapshots;

	//add to linked list
	l->next = node_map_start;
	node_map_start = l;

	node_count++;
}

/*
 * Must map to core 1 of any chip used!
 * Must map to core 0,0,1 (i.e. core 1 or root chip)!
 * Allocated space for logs and snapshots in NodeMapItems passed to HardwareMapping
 **/
void MapNodes()
{
	unsigned int i;
	NodeMapItemList *n;
	NodeMapItemList *temp;
	unsigned int next_core;
	unsigned int next_chip_x;
	unsigned int next_chip_y;

	//init hardware mapping hash
	MappingHashSize = (node_count)*2;
	MappingHash = malloc(sizeof(HardwareMapping)*MappingHashSize);
	ReverseMappingHash = malloc(sizeof(HardwareMapping)*MappingHashSize);
	memset(MappingHash, 0 , sizeof(HardwareMapping)*MappingHashSize);
	memset(ReverseMappingHash, 0 , sizeof(HardwareMapping)*MappingHashSize);

    //init hardware mappings
    next_core = 0;
    next_chip_x = 0;
    next_chip_y = 0;

    //iterate node map list to create mappings
	n = node_map_start;
	while (n != NULL){
		HardwareMapping hardware_mapping;
		hardware_mapping.damson_node_id = n->node_map_item.damson_node_id;

		//linear mapping: get next available core
		next_core++;
		if (next_core > 16){
			next_chip_x++;
			next_core = 1;
		}if (next_chip_x > (spinnaker_layout_width-1)){
			next_chip_y++;
			next_chip_x = 0;
		}if(next_chip_y > (spinnaker_layout_height-1)){
			printf("Error: Mapper has run out of available SpiNNaker cores\n");
			exit(0);
		}
		hardware_mapping.spinnaker_id = (next_chip_x << 16) + (next_chip_y << 8) + next_core;
		//copy node map info (prototype name??)
		hardware_mapping.num_logs = n->node_map_item.num_logs;
		hardware_mapping.logs = n->node_map_item.logs;
		hardware_mapping.num_snapshots = n->node_map_item.num_snapshots;
		hardware_mapping.snapshots = n->node_map_item.snapshots;

		//set core map
		core_map[next_chip_y + (next_chip_x*spinnaker_layout_width)] |= 1<<next_core;

		AddMapping(hardware_mapping);
		AddReverseMapping(hardware_mapping);

		n = n->next;
	}

	//iterate node map list to create routing tables
	n = node_map_start;
	while (n != NULL){
		for (i=0; i< n->node_map_item.num_interrupts; i++){
			if (n->node_map_item.interrupts[i] != 0)	//dont map timer interrupt
				Route(n->node_map_item.interrupts[i], n->node_map_item.damson_node_id);
		}
		n = n->next;
	}

	//cleanup link list
	n = node_map_start;
	while (n != NULL){
		//iterate and remove mapping
		temp = n->next;
		free(n->node_map_item.interrupts);
		free(n);
		n = temp;
	}
}


void LoadNode(unsigned int    node,
			  char            *prototype_object_name,
			  int             *gv,       unsigned int gvusersize,
			  int             *ev,       unsigned int evsize,
			  InterruptVector *intv,     unsigned int intvsize,
			  RuntimeLogItem  *logs,     unsigned int num_logs,
			  RuntimeLogItem  *snapshots,unsigned int num_snapshots,
			  int debug_mode)
{
	InterruptVector *InterruptHash;
	HardwareMapping map;
	SpiNN_address node_address;
	SpiNN_chip_address chip_address;
	unsigned int chip;
	unsigned int gv_size_words;
	unsigned int gv_size_bytes;
	unsigned int gv_user_size_bytes;
	unsigned int intv_hash_size;
	unsigned int intv_hash_size_bytes;
	unsigned int ev_size_bytes;
	unsigned int dtcm_data_size;
	unsigned int gv_start;
	unsigned int gv_user_start;
	unsigned int intv_start;
	unsigned int ev_start;
	unsigned int logs_start;
	unsigned int snapshots_start;
	unsigned int logs_size_bytes;
	unsigned int snapshots_size_bytes;

	gv_user_size_bytes = gvusersize *sizeof(int);
	gv_size_words = gvusersize + DAMSONRT_SYSTEM_RESERVED;
	gv_size_bytes = gv_size_words * sizeof(int);
	ev_size_bytes = evsize * sizeof(int);
	intv_hash_size = NextPower2(intvsize * 2)+1;		//must be a power of 2 (plus one for timer)
	intv_hash_size_bytes = intv_hash_size * sizeof(InterruptVector);
	logs_size_bytes = num_logs * sizeof(RuntimeLogItem);
	snapshots_size_bytes = num_snapshots * sizeof(RuntimeLogItem);

	gv_start = DAMSONRT_DTCM_START;  /* byte address */
	gv_user_start = gv_start + DAMSONRT_SYSTEM_RESERVED; /* byte address */
	intv_start = gv_start + gv_size_bytes;  /* byte address */
	logs_start = intv_start + intv_hash_size_bytes;
	snapshots_start = logs_start + logs_size_bytes;
	dtcm_data_size = gv_size_bytes + intv_hash_size_bytes + logs_size_bytes + snapshots_size_bytes;


	//check the total DTCM size
	if (dtcm_data_size>DAMSONRT_DTCM_DATA_MAX)
	{
		printf("Error: node %d DTCM data part size (%d bytes) exceeds limit required to load application (%d bytes)\n", node, dtcm_data_size, DAMSONRT_DTCM_DATA_MAX);
		exit(0);
	}

	//get the mapping for the current node and uncompress to a spinnaker address structure
	map = GetMapping(node);
	node_address = GetSpiNNAddress(map.spinnaker_id);
	chip_address.x = node_address.x;
	chip_address.y = node_address.y;
	chip = chip_address.y + (chip_address.x*spinnaker_layout_width);


	//get the ev start address based on the core number and update the aplx header
	ev_start = DAMSONRT_EV_START(node_address.core_id);

	//aplx header for filling areas of spinnaker memory (APLX_FILL, start address, length, value)
	unsigned int init_aplx[12] = {0x00000003, DAMSONRT_DTCM_START, dtcm_data_size,   			0x00000000, //data part of dtcm (not the stacks)
								  0x00000003, ev_start,            ev_size_bytes+sizeof(int),	0x00000000,	//external vector (extra value is evsize)
								  0xffffffff, 0x00000000,          0x00000000,           		0x00000000};
	//write to system memory and execute
	spiNN_write_memory(node_address, (char *)init_aplx, 0xf5000000, sizeof(init_aplx));
	spiNN_start_application_at(node_address, 0xf5000000);
	usleep(10000); //need to sleep for enough time to let aplx complete or there will be validation errors!


	//build interrupt vector
	InterruptHash = (InterruptVector*) malloc(intv_hash_size_bytes);
	BuildDeviceIntVector(InterruptHash, intv, intvsize);

	//write system globals
	spiNN_write_memory(node_address, (char*)&gv_size_words, 		(unsigned int)DAMSONRT_SYSTEM_GLOBAL_ADDRESS(0), sizeof(unsigned int));		//0 = gv size (user + reserved)
	spiNN_write_memory(node_address, (char*)&intv_hash_size, 		(unsigned int)DAMSONRT_SYSTEM_GLOBAL_ADDRESS(5), sizeof(unsigned int));		//5 = intv size (number of entries)

	spiNN_write_memory(node_address, (char*)&num_logs,    			(unsigned int)DAMSONRT_SYSTEM_GLOBAL_ADDRESS(8), sizeof(unsigned int));		//8 = log count
	spiNN_write_memory(node_address, (char*)&num_snapshots,   		(unsigned int)DAMSONRT_SYSTEM_GLOBAL_ADDRESS(9), sizeof(unsigned int));		//9 = snapshot count

	spiNN_write_memory(node_address, (char*)&intv_start,     		(unsigned int)DAMSONRT_SYSTEM_GLOBAL_ADDRESS(40), sizeof(unsigned int));		//40 = intv start
	spiNN_write_memory(node_address, (char*)&logs_start,    		(unsigned int)DAMSONRT_SYSTEM_GLOBAL_ADDRESS(43), sizeof(unsigned int));		//43 = start address of logs
	spiNN_write_memory(node_address, (char*)&snapshots_start,   	(unsigned int)DAMSONRT_SYSTEM_GLOBAL_ADDRESS(44), sizeof(unsigned int));		//44 = start address of snapshots

	spiNN_write_memory(node_address, (char*)&spinnaker_chips,  		(unsigned int)DAMSONRT_SYSTEM_GLOBAL_ADDRESS(48), sizeof(unsigned int));		//48 = chip count
	spiNN_write_memory(node_address, (char*)&node,           		(unsigned int)DAMSONRT_SYSTEM_GLOBAL_ADDRESS(49), sizeof(unsigned int));		//49 = node number

	//debug mode
	if (debug_mode)
		spiNN_write_memory(node_address, (char*)&debug_mode,        (unsigned int)DAMSONRT_SYSTEM_GLOBAL_ADDRESS(24), sizeof(unsigned int));		//24 = debug mode

	//write ev size to start of EV
	spiNN_write_memory(node_address, (char*)&evsize, ev_start, sizeof(unsigned int));

	//intelligently write vectors to device (i.e. only non zero parts)
	spiNN_writenonzero_memory(node_address, (char*)gv, gv_user_start, gv_user_size_bytes);	//gv user globals only
	spiNN_writenonzero_memory(node_address, (char*)ev, ev_start+sizeof(unsigned int), evsize*sizeof(int)); //gv offset by 4 bytes
	spiNN_writenonzero_memory(node_address, (char*)InterruptHash, intv_start, intv_hash_size_bytes);

	//write logs to device
	spiNN_writenonzero_memory(node_address, (char*)logs, logs_start, logs_size_bytes);
	spiNN_writenonzero_memory(node_address, (char*)snapshots, snapshots_start, snapshots_size_bytes);

	//free interrupt vector
	free(InterruptHash);

	//load core map to sdram if first core from the chip (i.e. core_id == 1)
	if (node_address.core_id == 1){
		unsigned int device_address;
		device_address = DAMSONRT_EV_SHARED_START;
		//write the core map
		spiNN_write_memory(node_address, (char*)core_map, device_address, spinnaker_chips*sizeof(unsigned int));
		device_address += spinnaker_chips*sizeof(unsigned int);
		//write the number of routing table values
		spiNN_write_memory(node_address, (char*)&chips[chip].rt_count, device_address, sizeof(unsigned int));
		device_address += sizeof(unsigned int);
		//write the routing table
		spiNN_write_memory(node_address, (char*)&chips[chip].rt, device_address, chips[chip].rt_count*sizeof(RoutingEntry));
	}

	//load program to non data part of DTCM (start of space reserved for stack at runtime)
	if (spiNN_load_application_at(node_address, prototype_object_name, DAMSONRT_DTCM_PROGRAM_START) == SPINN_FAILURE)
	{
		printf("Error: Damson protoype program '%s' for node %d not found! Have you linked it!\n", prototype_object_name, node);
		exit(0);
	}
	#if LOADER_DEBUG == 1
		printf("\t\t[loader_debug] Node (%u) loaded '%s' to SpiNNaker(%d,%d,%d)\n", node, prototype_object_name, node_address.x, node_address.y, node_address.core_id);
		CheckNodeMemory(node, gv, gvusersize, ev, evsize, intv, intvsize, logs, num_logs, snapshots, num_snapshots);
	#endif

}

int CheckNodeMemory(unsigned int    node,
				 int             *gv,       unsigned int gvusersize,
				 int             *ev,       unsigned int evsize,
				 InterruptVector *intv,     unsigned int intvsize,
				 RuntimeLogItem  *logs,     unsigned int num_logs,
				 RuntimeLogItem  *snapshots,unsigned int num_snapshots)
{
	InterruptVector *InterruptHash;
	unsigned int i, j, r;
	int *device_gv;
	int *device_ev;
	InterruptVector *device_intv;
	unsigned int *device_core_map;
	unsigned int device_rt_count;
	RoutingEntry device_rt[MAX_ROUTING_TABLE_ENTRIES];
	RuntimeLogItem  *device_logs;
	RuntimeLogItem  *device_snapshots;
	HardwareMapping map;
	SpiNN_address node_address;
	unsigned int chip;
	unsigned int gv_size_words;
	unsigned int gv_size_bytes;
	unsigned int gv_user_size_bytes;
	unsigned int intv_hash_size;
	unsigned int intv_hash_size_bytes;
	unsigned int gv_start;
	unsigned int gv_user_start;
	unsigned int intv_start;
	unsigned int ev_start;
	unsigned int logs_start;
	unsigned int snapshots_start;
	unsigned int logs_size_bytes;
	unsigned int snapshots_size_bytes;


	gv_user_size_bytes = gvusersize *sizeof(int);
	gv_size_words = gvusersize + DAMSONRT_SYSTEM_RESERVED;
	gv_size_bytes = gv_size_words * sizeof(int);
	intv_hash_size = NextPower2(intvsize * 2)+1;	//power of 2 plus one for timer
	intv_hash_size_bytes = intv_hash_size * sizeof(InterruptVector);
	logs_size_bytes = num_logs * sizeof(RuntimeLogItem);
	snapshots_size_bytes = num_snapshots * sizeof(RuntimeLogItem);

	gv_start = DAMSONRT_DTCM_START;  /* byte address */
	gv_user_start = gv_start + DAMSONRT_SYSTEM_RESERVED; /* byte address */
	intv_start = gv_start + gv_size_bytes;  /* byte address */
	logs_start =  intv_start + intv_hash_size_bytes;
	snapshots_start = logs_start + logs_size_bytes;


	//allocate memory to copy device vectors
	device_gv = (int*) malloc(gvusersize*sizeof(int));
	device_ev = (int*) malloc(evsize*sizeof(int));
	device_intv = (InterruptVector*) malloc(intv_hash_size_bytes);
	device_core_map = (unsigned int*) malloc(spinnaker_chips*sizeof(unsigned int));
	device_logs = (RuntimeLogItem*) malloc(num_logs*(sizeof(RuntimeLogItem)));
	device_snapshots = (RuntimeLogItem*) malloc(num_snapshots*(sizeof(RuntimeLogItem)));
	memset(device_gv, 0, gvusersize*sizeof(int));
	memset(device_ev, 0, evsize*sizeof(int));
	memset(device_intv, 0, intvsize*2*sizeof(InterruptVector));
	memset(device_core_map, 0, spinnaker_chips*sizeof(unsigned int));


	//get the mapping for the current node and uncompress to a spinnaker address structure
	map = GetMapping(node);
	node_address = GetSpiNNAddress(map.spinnaker_id);;
	chip = node_address.y + (node_address.x*spinnaker_layout_width);
	//get the ev start address based on the core number and update the aplx header
	ev_start = DAMSONRT_EV_START(node_address.core_id);

	//build interrupt vector
	InterruptHash = (InterruptVector*) malloc(intv_hash_size_bytes);
	BuildDeviceIntVector(InterruptHash, intv, intvsize);

	//get vectors from device
	spiNN_read_memory(node_address, (char*)device_gv,   gv_user_start, gv_user_size_bytes);
	spiNN_read_memory(node_address, (char*)device_ev,   ev_start+sizeof(unsigned int), evsize*sizeof(int));
	spiNN_read_memory(node_address, (char*)device_intv, intv_start, intv_hash_size_bytes);

	//get logs from device
	spiNN_read_memory(node_address, (char*)device_logs, logs_start, logs_size_bytes);
	spiNN_read_memory(node_address, (char*)device_snapshots, snapshots_start, snapshots_size_bytes);

	//check gv
	r = 1;
	for (i=0; i<gvusersize; i++)
	{
		if (device_gv[i] != gv[i]){
			printf("Node (%d) GV Validation Failed at %d! host %8x != device %8x\n", node, i+DAMSONRT_SYSTEM_RESERVED, gv[i], device_gv[i]);
			r = 0;
		}
	}
	for (i=0; i<evsize; i++)
	{
		if (device_ev[i] != ev[i]){
			printf("Node (%d) EV Validation Failed at %d! host %d != device %d\n", node, i, ev[i], device_ev[i]);
			r = 0;
		}
	}
	for (i=0; i<intv_hash_size; i++)
	{
		if (device_intv[i].src_node != InterruptHash[i].src_node){
			printf("Node (%d) Interrupt Hashtable Validation Failed at %d! host %d != device %d\n", node, i, InterruptHash[i].src_node, device_intv[i].src_node);
			r = 0;
		}
	}
	if (node_address.core_id == 1){
		unsigned int device_address;

		//check the core map
		device_address = DAMSONRT_EV_SHARED_START;
		spiNN_read_memory(node_address, (char*)device_core_map, device_address, spinnaker_chips*sizeof(unsigned int));
		for (i=0; i< spinnaker_chips; i++)
		{
			unsigned int cm = core_map[i];
			if (cm != device_core_map[i]){
				printf("Node (%d) Core map Validation Failed at %d! host %d != device %d\n", node, i, cm, device_core_map[i]);
				r = 0;
			}
		}

		//check the routing table
		device_address += spinnaker_chips*sizeof(unsigned int);
		spiNN_read_memory(node_address, (char*)&device_rt_count, device_address, sizeof(unsigned int));
		if (device_rt_count != chips[chip].rt_count){
			printf("Node (%d) number of routing table entries does not match! host %d != device %d\n", node, chips[chip].rt_count, device_rt_count);
			r = 0;
		}else{
			device_address += sizeof(unsigned int);
			spiNN_read_memory(node_address, (char*)device_rt, device_address, device_rt_count*sizeof(RoutingEntry));

			for (i=0; i<device_rt_count; i++)
			{
				if (device_rt[i].key != chips[chip].rt[i].key){
					printf("Node (%d) routing entry %d key missmatch! host %d != device %d\n", node, i, chips[chip].rt[i].key, device_rt[i].key);
					r = 0;
				}
				if (device_rt[i].route != chips[chip].rt[i].route){
					printf("Node (%d) routing entry %d route missmatch! host %x != device %x\n", node, i, chips[chip].rt[i].route, device_rt[i].route);
					r = 0;
				}
			}

		}
	}
	//check logs
	for (i=0; i<num_logs; i++)
	{
		if (device_logs[i].handle != logs[i].handle){
			printf("Node (%d) Log %d 'handle' Validation Failed host %d != device %d\n", node, i, logs[i].handle, device_logs[i].handle);
			r = 0;
		}
		if (device_logs[i].start_time != logs[i].start_time){
			printf("Node (%d) Log %d 'start_time' Validation Failed host %d != device %d\n", node, i, logs[i].start_time, device_logs[i].start_time);
			r = 0;
		}
		if (device_logs[i].end_time != logs[i].end_time){
			printf("Node (%d) Log %d 'end_time' Validation Failed host %d != device %d\n", node, i, logs[i].end_time, device_logs[i].end_time);
			r = 0;
		}
		if (device_logs[i].interval != logs[i].interval){
			printf("Node (%d) Log %d 'interval' Validation Failed host %d != device %d\n", node, i, logs[i].interval, device_logs[i].interval);
			r = 0;
		}
		if (device_logs[i].log_items != logs[i].log_items){
			printf("Node (%d) Log %d 'log_items' Validation Failed host %d != device %d\n", node, i, logs[i].log_items, device_logs[i].log_items);
			r = 0;
		}
		for (j=0;j<logs[i].log_items; j++)
		{
			if (device_logs[i].log_globals[j] != logs[i].log_globals[j]){
				printf("Node (%d) Log %d log global %d Validation Failed host %d != device %d\n", node, i, j, logs[i].log_globals[j], device_logs[i].log_globals[j]);
				r = 0;
			}
		}
	}

	//check snapshots
	for (i=0; i<num_snapshots; i++)
	{
		if (device_snapshots[i].handle != snapshots[i].handle){
			printf("Node (%d) Snapshot %d 'handle' Validation Failed host %d != device %d\n", node, i, snapshots[i].handle, device_snapshots[i].handle);
			r = 0;
		}
		if (device_snapshots[i].start_time != snapshots[i].start_time){
			printf("Node (%d) Snapshot %d 'start_time' Validation Failed host %d != device %d\n", node, i, snapshots[i].start_time, device_snapshots[i].start_time);
			r = 0;
		}
		if (device_snapshots[i].end_time != snapshots[i].end_time){
			printf("Node (%d) Snapshot %d 'end_time' Validation Failed host %d != device %d\n", node, i, snapshots[i].end_time, device_snapshots[i].end_time);
			r = 0;
		}
		if (device_snapshots[i].interval != snapshots[i].interval){
			printf("Node (%d) Snapshot %d 'interval' Validation Failed host %d != device %d\n", node, i, snapshots[i].interval, device_snapshots[i].interval);
			r = 0;
		}
		if (device_snapshots[i].log_items != snapshots[i].log_items){
			printf("Node (%d) Snapshot %d 'log_items' Validation Failed host %d != device %d\n", node, i, snapshots[i].log_items, device_snapshots[i].log_items);
			r = 0;
		}
		for (j=0;j<snapshots[i].log_items; j++)
		{
			if (device_snapshots[i].log_globals[j] != snapshots[i].log_globals[j]){
				printf("Node (%d) Snapshot %d log global %d Validation Failed host %d != device %d\n", node, i, j, snapshots[i].log_globals[j], device_snapshots[i].log_globals[j]);
				r = 0;
			}
		}
	}


	#if LOADER_DEBUG == 1
			printf("\t\t[loader_debug] Node (%d) at SpiNNaker(%d, %d, %d) passed memory check\n", node, node_address.x, node_address.y, node_address.core_id);
	#endif

	//free
	free(device_gv);
	free(device_ev);
	free(device_intv);
	free(device_core_map);
	free(InterruptHash);
	free(device_logs);
	free(device_snapshots);

	return r;
}


void Start()
{
	int x, y, i;
	unsigned int  cm;
	SpiNN_address node_address;
	HardwareMapping map;

	//iterate the core map to start cores (always start core 1 last, always start chip 0,0 last)
	for (x=spinnaker_layout_width-1; x>=0; x--)
		{
			for (y=spinnaker_layout_height-1; y>=0; y--)
			{
				cm = core_map[y + (x*spinnaker_layout_width)];
				for (i=16; i>0; i--){	//reverse order
					if ((cm>>i) & 1)	//if active
					{
						//check that there is a mapping (if not something is wrong with MapNodes!!)
						map = GetReverseMapping((x << 16) + (y << 8) + i);
						node_address = GetSpiNNAddress(map.spinnaker_id);

					#if LOADER_DEBUG == 1
						printf("\t\t[loader_debug] Starting Node (%d) at SpiNNaker(%d, %d, %d)\n", map.damson_node_id, node_address.x, node_address.y, node_address.core_id);
					#endif

						spiNN_start_application_at(node_address, DAMSONRT_DTCM_PROGRAM_START);
					}
				}
			}
		}

	//block (let io thread handle printf)
	while(spinnaker_running){
		//do nothing, wait for exit command
	}

	//handle any log or snapshot data and cleanup
	for (x=spinnaker_layout_width-1; x>=0; x--)
	{
		for (y=spinnaker_layout_height-1; y>=0; y--)
		{
			cm = core_map[y + (x*spinnaker_layout_width)];
			for (i=16; i>0; i--){	//reverse order
				if ((cm>>i) & 1)	//if active
				{
					unsigned int log_position;
					unsigned int log_data_start;
					unsigned int log_data_end;
					unsigned int log_data_size_bytes;
					unsigned int *log_data;
					unsigned int *log_entry;

					//check that there is a mapping (if not something is wrong with MapNodes)
					map = GetReverseMapping((x << 16) + (y << 8) + i);
					node_address = GetSpiNNAddress(map.spinnaker_id);

					//get the size of the external external vector and end address of log data items
					spiNN_read_memory(node_address, (char*)&log_data_start, DAMSONRT_EV_START(node_address.core_id), sizeof(int));
					spiNN_read_memory(node_address, (char*)&log_data_end, (unsigned int)DAMSONRT_SYSTEM_GLOBAL_ADDRESS(25), sizeof(int));
					//log data starts at the end of user external vector (plus one is for the ev size at the start)
					log_data_start = (unsigned int)DAMSONRT_EV_START(node_address.core_id) + BYTES(log_data_start) + sizeof(int);

					//calc size of log data
					log_data_size_bytes = log_data_end-log_data_start;

					//init some memory and then get the log data
					log_data = (unsigned int*)malloc(log_data_size_bytes);
					spiNN_read_memory(node_address, (char*)log_data, log_data_start, log_data_size_bytes);


					log_position = 0;

					//initialise log file for writing
					if (log_data_size_bytes > 0)
						InitLogFiles(map);

					//save any log data
					while (BYTES(log_position) < log_data_size_bytes)
					{
						//point log entry to the current position in the log data
						log_entry = &log_data[log_position];

						if (log_entry[1] > MAX_LOG_ITEMS)
						{
							printf("Warning: Possible log corruption. Log entry has too many items '%d'!\n", log_entry[1]);
							break;
						}

						//increment the log position by 2 integers (handle and num entries) and the number of entries
						log_position += 2 + log_entry[1];

						OutputLogEntry(map, log_entry[0], log_entry[1], &log_entry[2]);
					}

					//close the log files
					if (log_data_size_bytes > 0)
						CloseLogFiles(map);

					//free to log data
					free(log_data);
					//free the logs and snapshots
					free(map.logs);
					free(map.snapshots);
				}
			}
		}
	}
}


/* Private functions */

void InitLogFiles(HardwareMapping map)
{
	unsigned int i;

	//logs
	for (i=0; i<map.num_logs; i++)
	{
		FILE *f = fopen(map.logs[i].filename, "w");

		if (f == NULL)
		{
			printf("Warning: unable to open log file for log '%s'\n", map.logs[i].filename);
		}

		map.logs[i].outputfile = f;
	}

	//snapshots
	for (i=0; i<map.num_snapshots; i++)
	{
		FILE *f = fopen(map.snapshots[i].filename, "w");

		if (f == NULL)
		{
			printf("Warning: unable to open snapshot file for snapshot '%s'\n", map.snapshots[i].filename);
		}

		map.snapshots[i].outputfile = f;
	}

}

void CloseLogFiles(HardwareMapping map)
{
	unsigned int i;

	for (i=0; i<map.num_logs; i++)
	{
		fclose(map.logs[i].outputfile);
	}
	for (i=0; i<map.num_snapshots; i++)
	{
		fclose(map.snapshots[i].outputfile);
	}

}

void OutputLogEntry(HardwareMapping map, unsigned int handle, unsigned int log_items, unsigned int *log_values)
{
	unsigned int i;
	//find the correct handle
	for (i=0; i<map.num_logs; i++)
	{
		if (map.logs[i].handle == handle)
		{
			if (map.logs[i].log_items != log_items){
				printf("Warning: skipping miss-matched number of log items (%d) from runtime (%d) for log '%s'\n", map.logs[i].log_items, log_items, map.logs[i].filename);
				return;
			}
			//some kind of printf
			if (MAX_LOG_ITEMS != 5)
			{
				printf("Warning: Loader MAX_LOG_ITEMS %d should be 10\n", MAX_LOG_ITEMS);
				return;
			}
			Damson_fprintf(map.logs[i].outputfile, map.logs[i].format, log_values[0], log_values[1],
																	   log_values[2], log_values[3],
																	   log_values[4]);

			return;
		}
	}
	for (i=0; i<map.num_snapshots; i++)
	{
		if (map.snapshots[i].handle == handle)
		{
			if (map.snapshots[i].log_items != log_items){
				printf("Warning: skipping miss-matched number of snapshot items from runtime for snapshot '%s'\n", map.snapshots[i].filename);
				return;
			}
			//some kind of printf
			Damson_fprintf(map.snapshots[i].outputfile, map.snapshots[i].format, log_values[0], log_values[1],
																				 log_values[2], log_values[3],
																				 log_values[4], log_values[5],
																				 log_values[6], log_values[7],
																				 log_values[8], log_values[9]);
			return;
		}
	}
}

/**
 * General hash function using the runtime system hash values
 */
unsigned int Hash(unsigned int n, unsigned int size)
{
    unsigned int h;
    h = (n * DAMSONRT_HASH_A + DAMSONRT_HASH_C) % size;
    return h;
}

/**
 * Function to add a mapping to the mapping ,3,4,2,2,4,3,hash table
 */
void AddMapping(HardwareMapping mapping)
{
	unsigned int h;
	unsigned int n;

	n = 0;
	h = Hash(mapping.damson_node_id, MappingHashSize);
	while (MappingHash[h].damson_node_id != 0)
	{
		n++;
		if (n >= MappingHashSize)
		{
			//should never be the case as hash table is twice the expected size
			printf("Error: Mapping hash table overflow\n");
			exit(0);
		}
		h++;
		if (h >= MappingHashSize)
		{
			h = 0;
		}
	}
	MappingHash[h] = mapping;
}

void AddReverseMapping(HardwareMapping mapping)
{
	unsigned int h;
	unsigned int n;

	n = 0;
	h = Hash(mapping.spinnaker_id, MappingHashSize);
	while (ReverseMappingHash[h].spinnaker_id != 0)
	{
		n++;
		if (n >= MappingHashSize)
		{
			//should never be the case as hash table is twice the expected size
			printf("Error: Reverse mapping hash table overflow\n");
			exit(0);
		}
		h++;
		if (h >= MappingHashSize)
		{
			h = 0;
		}
	}
	ReverseMappingHash[h] = mapping;
}

HardwareMapping GetMapping(unsigned int node_id)
{
    unsigned int    h;
    unsigned int    n;
    HardwareMapping b;

    if (MappingHashSize == 0){
    	printf("Error: Loader not initialised or no mappings in the mapping file\n");
    	exit(0);
    }

    n = 0;
    h = Hash(node_id, MappingHashSize);

    while (MappingHash[h].damson_node_id != 0)
    {
        b = MappingHash[h];

        if (b.damson_node_id == node_id)
        {
            return b;
        }

        n += 1;
        if (n >= MappingHashSize)
        {
        	break;
        }
        h += 1;
        if (h >= MappingHashSize)
        {
            h = 0;
        }
    }
    //no default return required exit if no mapping is found
    printf("Error: Node Number '%u' does not exist in mapping file\n", node_id);
    exit(0);
}

HardwareMapping GetReverseMapping(unsigned int spinnaker_id)
{
    unsigned int    h;
    unsigned int    n;
    HardwareMapping b;

    if (MappingHashSize == 0){
    	printf("Error: Loader not initialised or no mappings in the mapping file\n");
    	exit(0);
    }

    n = 0;
    h = Hash(spinnaker_id, MappingHashSize);

    while (ReverseMappingHash[h].spinnaker_id != 0)
    {
        b = ReverseMappingHash[h];

        if (b.spinnaker_id == spinnaker_id)
        {
            return b;
        }

        n += 1;
        if (n >= MappingHashSize)
        {
        	break;
        }
        h += 1;
        if (h >= MappingHashSize)
        {
            h = 0;
        }
    }
    //no default return required exit if no mapping is found
    printf("Error: SpiNNaker address '%u' does not exist in mapping file\n", spinnaker_id);
    exit(0);
}

unsigned int NextPower2(unsigned int hash_list_size)
{
	int power = 1;
	while(power < hash_list_size)
	    power*=2;
	return power;
}

SpiNN_address GetSpiNNAddress(unsigned int spinnaker_id)
{
	SpiNN_address s;
	s.core_id = spinnaker_id & 255;
	s.x = spinnaker_id >> 16;
	s.y = (spinnaker_id >> 8) & 255;

	return s;
}

void BuildDeviceIntVector(InterruptVector *int_hash, InterruptVector *intv, unsigned int intvsize)
{
	unsigned int i;
	unsigned int intv_hash_size;

	//reset interrupt vector
	intv_hash_size = NextPower2(intvsize*2)+1;
	memset(int_hash, 0, intv_hash_size*sizeof(InterruptVector));
	intv_hash_size--;	//reduce by 1 as timer is special case

	//Iterate and build interrupt vector for device
	for (i=0; i<intvsize; i++){
		unsigned int n, h;
		//timer interrupt (special case at front of vector)
		if (intv[i].src_node == 0){
			int_hash[0].count ++;
			int_hash[0].code_offset = intv[i].code_offset;
			continue;
		}
		//pkt interrupt
		h = Hash(intv[i].src_node, intv_hash_size)+1;
		n = 0;
		while ((int_hash[h].src_node != 0)&&(int_hash[h].src_node != intv[i].src_node))
		{
			n++;
			if (n >= intv_hash_size)
			{
				printf("Error: Interrupt hash table overflow\n");
				exit(0);
			}
			h++;
			if (h >= intv_hash_size)
			{
				h = 1;
			}
		}
		int_hash[h].count ++;
		int_hash[h].src_node = intv[i].src_node;
		int_hash[h].code_offset = intv[i].code_offset;
	}
}


void HandleDebugMessage(SpiNN_address address, char* message)
{
	int msg_len;
	HardwareMapping src_node;

	if (!spinnaker_running)
		return;

	//calc srs node
	src_node.spinnaker_id = (address.x << 16) + (address.y << 8) + address.core_id;
	src_node = GetReverseMapping(src_node.spinnaker_id);

	//remove new line as this is enforced
	msg_len = strlen(message);
	if (message[msg_len-1] == '\n')
		message[msg_len-1] = '\0';

	//check for special HOSTCMD packets
	if (strncmp(message, "HOSTCMD:", 8) == 0){
		#if LOADER_DEBUG == 1
			printf("\t\t[loader_debug] received HOSTCMD '%s' from SpiNNaker(%d, %d, %d)\n", &message[8], address.x, address.y, address.core_id);
		#endif
		if (strncmp(&message[8], "exit", 4) == 0){
			printf("Node (%d) exit %s\n", src_node.damson_node_id, &message[13]);
		}
		else if (strncmp(&message[8], "ticks", 5) == 0){
			printf("SpiNNaker ticks: %s\n", &message[14]);
		}
		else if (strncmp(&message[8], "shutdown", 8) == 0){
			printf("SpiNNaker time: %s ms\n", &message[17]);
			spinnaker_running = 0;
		}
		return;
	}

	#if LOADER_DEBUG == 1
		printf("%d(%d,%d,%d)\t%s\n", src_node.damson_node_id, address.x, address.y, address.core_id, message);
	#else
		printf("%d\t%s\n", src_node.damson_node_id, message);
	#endif


}



/**
 * Route a source and destination damson node by creating routing table entries for the necessary chips.
 * Currently assumes no wrap around!
 */
void Route(unsigned int src_id, unsigned int dst_id)
{
	HardwareMapping src_mapping, dst_mapping;
	SpiNN_address src_adr, dst_adr, tmp_adr;
	unsigned int chip_index, route;

	src_mapping = GetMapping(src_id);
	dst_mapping = GetMapping(dst_id);

	src_adr = GetSpiNNAddress(src_mapping.spinnaker_id);
	dst_adr = GetSpiNNAddress(dst_mapping.spinnaker_id);
	tmp_adr = src_adr;

	#if LOADER_DEBUG == 1
		printf("\t\t[loader_debug] Routing node %d (%d,%d,%d) to %d ", src_id, src_adr.x, src_adr.y, src_adr.core_id, dst_id);
	#endif

	while ((tmp_adr.x != dst_adr.x) || (tmp_adr.y != dst_adr.y)) {
		chip_index = tmp_adr.y + (tmp_adr.x * spinnaker_layout_width); //chip index before updating hop
		route = 0;
		#if LOADER_DEBUG == 1
			printf(" -> chip(%d,%d)", tmp_adr.x, tmp_adr.y);
		#endif

		if ((tmp_adr.x < dst_adr.x) && (tmp_adr.y == dst_adr.y)) {
			route = LINK_EAST;
			tmp_adr.x += 1;
		} else if ((tmp_adr.x > dst_adr.x) && (tmp_adr.y == dst_adr.y)) {
			route = LINK_WEST;
			tmp_adr.x -= 1;
		}

		else if ((tmp_adr.x == dst_adr.x) && (tmp_adr.y < dst_adr.y)) {
			route = LINK_NORTH;
			tmp_adr.y += 1;
		} else if ((tmp_adr.x == dst_adr.x) && (tmp_adr.y > dst_adr.y)) {
			route = LINK_SOUTH;
			tmp_adr.y -= 1;
		}

		else if ((tmp_adr.x < dst_adr.x) && (tmp_adr.y < dst_adr.y)) {
			route = LINK_NORTH_EAST;
			tmp_adr.x += 1;
			tmp_adr.y += 1;
		} else if ((tmp_adr.x > dst_adr.x) && (tmp_adr.y > dst_adr.y)) {
			route = LINK_SOUTH_WEST;
			tmp_adr.x -= 1;
			tmp_adr.y -= 1;
		}

		//special cases where there is no direct mapping
		else if ((tmp_adr.x < dst_adr.x) && (tmp_adr.y > dst_adr.y)) {
			route = LINK_EAST;
			tmp_adr.x += 1;
		} else if ((tmp_adr.x > dst_adr.x) && (tmp_adr.y < dst_adr.y)) {
			route = LINK_WEST;
			tmp_adr.x -= 1;
		}

		createRoutingEntry(chip_index, src_id<< DAMSONRT_PORT_BITS, route);
	}

	#if LOADER_DEBUG == 1
		printf(" -> cpu(%d)\n", dst_adr.core_id);
	#endif

	chip_index = dst_adr.y + (dst_adr.x * spinnaker_layout_width);
	route = (1 << (NUM_LINKS + dst_adr.core_id));
	//create core mapping
	createRoutingEntry(chip_index, src_id<< DAMSONRT_PORT_BITS, route);
}

void createRoutingEntry(unsigned int chip_index, unsigned int src_id, unsigned int route){
	unsigned int i;
	ChipConfig *c;

	c = &chips[chip_index];

	//see if there is an existing entry for the key
	for (i=0; i < c->rt_count; i++)
	{
		if (c->rt[i].key == src_id){
			c->rt[i].route |= route;
			return;
		}
	}

	//if no existing key then create a new one
	if (c->rt_count >= MAX_ROUTING_TABLE_ENTRIES){
		printf("Error: Chip %d routing table overflow\n", chip_index);
		exit(0);
	}
	c->rt[c->rt_count].key = src_id ;
	c->rt[c->rt_count].route = route;
	c->rt_count++;
}

/* From DAMSON emulator */
void Damson_fprintf(FILE *stream, char *fmt, ...)
{
    va_list  	 list;
    char         *p, *r;
    int          e;
    double       x;
    char         c;
    long int     u;
    char         str[50];
    unsigned int q;
    char         t;
    int      	 sfound;

    va_start(list, fmt);

    for (p=fmt; *p; ++p)
    {
        if (*p != '%')
        {
            fprintf(stream, "%c", *p);
        }
        else
        {
            q = 1;
            sfound = 0;
            str[0] = '%';

            do
            {
                t = *++p;
                str[q] = t;
                q += 1;

                switch (t)
                {
                    case 'd': case 'i': case 'o': case 'x': case 'X': case 'u': case 'c':
                    case 's': case 'f': case 'e': case 'E': case 'g': case 'G': case 'p': case '%':
                        str[q] = '\0';
                        sfound = 1;
                        break;

                    default:
                        break;
                }
            } while (!sfound);

            switch(t)
            {
                case 'd': case 'i': case 'o': case 'x': case 'X': case 'u':
                    e = va_arg(list, int);
                    fprintf(stream, str, e);
                    continue;

                case 'c':
                    c = va_arg(list, int);
                    fprintf(stream, str, c);
                    continue;

                case 's':
                    r = va_arg(list, char *);
                    fprintf(stream, str, r);
                    continue;

                case 'f': case 'e': case 'E': case 'g': case 'G':
                    x = (double) va_arg(list, int) / 65536.0;
                    fprintf(stream, str, x);
                    continue;

                case 'p':
                    u = (long int) va_arg(list, void *);
                    fprintf(stream, str, u);
                    continue;

                case '%':
                    fprintf(stream, "%%");
                    continue;

                default:
                    fprintf(stream, "%c", *p);
            }
        }
    }
    va_end(list);
}

