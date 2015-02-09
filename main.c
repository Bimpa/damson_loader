
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

#include "loader.h"
#include "damson_runtime.h"

#define WAIT_UNTIL_EXIT 10

#define DAMSONRT_MAX_GV_WORDS 		10000
#define DAMSONRT_MAX_INTV_ITEMS 	1000
#define DAMSONRT_MAX_LOGS			10

#define MAX_DEBUG_NODES				10


unsigned int getword(FILE *stream);
void		 skipword(FILE *stream);
void         getstring(char str[], unsigned int str_len, FILE *stream);
void		 skipstring(FILE *stream);

int main(int argc, char *argv[])
{
    FILE          *FileStream;
    unsigned int  debug_list[MAX_DEBUG_NODES];
    unsigned int  n;
    unsigned int  gv_size;
    unsigned int  ev_size;
    unsigned int  interrupts;
	unsigned int  total_logs;
    unsigned int  num_logs;
    unsigned int  num_snapshots;
    unsigned int  debug_mode;
    unsigned long long int t1, t2;
    struct timeval tv;
    int* gv;
    int* ev;
    InterruptVector* iv;
    RuntimeLogItem* logs;
    RuntimeLogItem* snapshots;
    char prototype_name[100];
	unsigned int i, j;


	if (argc < 2){
		printf("Usage is: linker <linker_file> <debug_nodes>\n");
		printf("\te.g. linker example.lnk\n");
		printf("\tor   linker example.lnk 1 2 3\n");
	}

	//debug items
	memset(debug_list, 0, sizeof(int)*MAX_DEBUG_NODES);
	for(i=2;i<argc;i++){
		j = i-2;
		if(j==MAX_DEBUG_NODES){
			printf("Warning: Maximum number of debug nodes is %d\n", MAX_DEBUG_NODES);
			break;
		}
		debug_list[j] = atoi(argv[i]);
	}

    gv = malloc(sizeof(int)*DAMSONRT_MAX_GV_WORDS);
	ev = malloc(DAMSONRT_EV_SIZE);
	iv = malloc(sizeof(InterruptVector)*DAMSONRT_MAX_INTV_ITEMS);
	logs = malloc(sizeof(LoaderLogItem)*DAMSONRT_MAX_LOGS);
	snapshots = malloc(sizeof(LoaderLogItem)*DAMSONRT_MAX_LOGS);

    FileStream = fopen(argv[1], "rb");
    if (FileStream == NULL)
    {
        printf("No file %s\n", argv[1]);
        exit(1);
    }

    InitLoader();

	gettimeofday(&tv, NULL);
	t1 = tv.tv_sec * 1000 + tv.tv_usec/1000;

    //1) first loop through alias data to set mappings
	while(1)
	{
		NodeMapItem node_map;
		LoaderLogItem* temp_logs;

		//init
		num_logs = 0;
		num_snapshots = 0;
		node_map.num_logs = 0;
		node_map.num_snapshots = 0;

		//node number
		node_map.damson_node_id = getword(FileStream);
		if (node_map.damson_node_id  == 0)
		{
			break;
		}
		//skip node alias data
		skipstring(FileStream);
		gv_size = getword(FileStream);
		gv_size++;	//first gv value is 0
		for (i=0; i<gv_size; i++)
		{
			skipword(FileStream);
		}

		ev_size = getword(FileStream);
		for (i=0; i<ev_size; i++)
		{
			skipword(FileStream);
		}

		//get node interrupt data
		node_map.num_interrupts = getword(FileStream);
		node_map.interrupts = (unsigned int*)malloc(node_map.num_interrupts * sizeof(unsigned int)); //allocated here free'd in NodeMapItem by MapNodes()
		for (i=0; i<node_map.num_interrupts; i++)
		{
			skipword(FileStream); //ignore code position
			node_map.interrupts[i] = getword(FileStream);
		}

		//get all logs and snapshots as these are not separate in the loader file!!!!
		total_logs = getword(FileStream);
		temp_logs = (LoaderLogItem*)malloc(total_logs * sizeof(LoaderLogItem));
		for (i=0; i<total_logs; i++)
		{
			//count logs vs snapshots
			temp_logs[i].handle = getword(FileStream);
			if (temp_logs[i].handle == 1)
				num_logs++;
			else
				num_snapshots++;
			skipword(FileStream); //ignore start_time
			skipword(FileStream); //ignore end_time
			skipword(FileStream); //ignore interval
			temp_logs[i].log_items = getword(FileStream); //duplicated in log entry but so what
			for (j=0;j<temp_logs[i].log_items; j++){
				skipword(FileStream);
			}
			getstring(temp_logs[i].format, MAX_STRING_SIZE, FileStream);
			getstring(temp_logs[i].filename, MAX_STRING_SIZE, FileStream);
		}

		//now sort them out
		node_map.logs = (LoaderLogItem*)malloc(num_logs * sizeof(LoaderLogItem));			//allocated here free'd in HardwareMapping at the end of Start()
		node_map.snapshots = (LoaderLogItem*)malloc(num_snapshots * sizeof(LoaderLogItem));	//allocated here free'd in HardwareMapping at the end of Start()
		for (i=0; i<total_logs; i++)
		{
			if (temp_logs[i].handle == 1)
			{
				memcpy(&node_map.logs[node_map.num_logs], &temp_logs[i], sizeof(LoaderLogItem));
				node_map.logs[node_map.num_logs].handle = i;
				node_map.num_logs++;
			}else
			{
				memcpy(&node_map.snapshots[node_map.num_snapshots], &temp_logs[i], sizeof(LoaderLogItem));
				node_map.snapshots[node_map.num_snapshots].handle = i;
				node_map.num_snapshots++;
			}
		}
		free(temp_logs);

		AddNodeMapItem(&node_map);
	}

	//map nodes
	MapNodes();

	//reset
	rewind(FileStream);

    //2) second loop through alias data for loading
    while(1)
    {
    	//node number
        n = getword(FileStream);
        if (n == 0)
        {
            break;
        }
        //prototype name
        getstring(prototype_name, MAX_STRING_SIZE, FileStream);
        //global vector
        gv_size = getword(FileStream);
        gv_size++; //first gv value is 0
        if (gv_size>DAMSONRT_MAX_GV_WORDS){
			printf("Node %d global vector words '%d' exceeds loader maximum '%d'\n", n, gv_size, DAMSONRT_MAX_GV_WORDS);
			exit(1);
		}
		for (i=0; i<gv_size; i++)
		{
			gv[i] = getword(FileStream);
		}
        //external vector
        ev_size = getword(FileStream);
        if (ev_size>(DAMSONRT_EV_SIZE/4)){
			printf("Node %d external vector words '%d' exceeds loader maximum '%d'\n", n, ev_size, DAMSONRT_EV_SIZE/4);
			exit(1);
		}
		for (i=0; i<ev_size; i++)
		{
			ev[i] = getword(FileStream);
		}

        //interrupt vector
        interrupts = getword(FileStream);
        if (interrupts > DAMSONRT_MAX_INTV_ITEMS){
			printf("Node %d interrupt vector entries '%d' exceeds loader maximum '%d'\n", n, interrupts, DAMSONRT_MAX_INTV_ITEMS);
			exit(1);
		}
		for (i=0; i<interrupts; i++)
		{
			InterruptVector *interrrupt = &iv[i];
			interrrupt->code_offset= getword(FileStream);
			interrrupt->src_node = getword(FileStream);
		}

		//get all logs
		total_logs = getword(FileStream);
		num_logs = 0;
		num_snapshots = 0;
		for (i=0; i<total_logs; i++)
		{
			unsigned int log_type;
			RuntimeLogItem *log;

			log_type = getword(FileStream);
			if (log_type == 1)
				log = &logs[num_logs++];
			else
				log = &snapshots[num_snapshots++];

			log->handle = i;
			log->start_time = getword(FileStream);
			log->end_time = getword(FileStream);
			log->interval = getword(FileStream);
			log->interval_count = log->interval; //also set at runtime
			log->log_items = getword(FileStream);
			if (log->log_items > MAX_LOG_ITEMS){
				printf("Node %d log has more items '%d' than maximum '%d'\n", n,log->log_items, MAX_LOG_ITEMS);
				exit(1);
			}
			for (j=0; j<MAX_LOG_ITEMS; j++){
				if (j < log->log_items)
					log->log_globals[j] = (sizeof(int)*getword(FileStream)) + DAMSONRT_DTCM_START;
			}
			skipstring(FileStream);
			skipstring(FileStream);
		}

		//debug mode
		debug_mode = 0;
		for (i=0;i<MAX_DEBUG_NODES;i++){
			if (debug_list[i] == n)
				debug_mode = 1;
		}

        //load and check
		LoadNode(n, prototype_name, gv, gv_size, ev, ev_size, iv, interrupts, logs, num_logs, snapshots, num_snapshots, debug_mode);

    }
    gettimeofday(&tv, NULL);
    t2 = tv.tv_sec * 1000 + tv.tv_usec/1000;

    fclose(FileStream);

    free(gv);
    free(ev);
    free(iv);
    free(logs);
    free(snapshots);

    Start();



    printf("Loading time: %lld ms\n", t2-t1);

    ExitLoader();

    return 0;
}

/* -------------------------------------------------- */
unsigned int getword(FILE *stream)
{
	int a, b, c, d;
	a = fgetc(stream);
	if (a == EOF)
	{
		printf("Unexpected end found in linker file!\n");
		exit(1);
	}
	b = fgetc(stream);
	if (b == EOF)
	{
		printf("Unexpected end found in linker file!\n");
		exit(1);
	}
	c = fgetc(stream);
	if (c == EOF)
	{
		printf("Unexpected end found in linker file!\n");
		exit(1);
	}
	d = fgetc(stream);
	if (d == EOF)
	{
		printf("Unexpected end found in linker file!\n");
		exit(1);
	}
    return (a << 24) | (b << 16) | (c << 8) | d;
}

void skipword(FILE *stream)
{
	int a, b, c, d;
	a = fgetc(stream);
	if (a == EOF)
	{
		printf("Unexpected end found in linker file!\n");
		exit(1);
	}
	b = fgetc(stream);
	if (b == EOF)
	{
		printf("Unexpected end found in linker file!\n");
		exit(1);
	}
	c = fgetc(stream);
	if (c == EOF)
	{
		printf("Unexpected end found in linker file!\n");
		exit(1);
	}
	d = fgetc(stream);
	if (d == EOF)
	{
		printf("Unexpected end found in linker file!\n");
		exit(1);
	}
}

/* -------------------------------------------------- */
void getstring(char str[], unsigned int str_len, FILE *stream)
{
    unsigned int p = 0;
    while (1)
    {
        str[p] = fgetc(stream);
        if (str[p] == '\0')
        {
            break;
        }
        p += 1;
        if (p == str_len){
        	str[p] = '\0';
        	printf("String too long for buffer '%s...'!\n", str);
        	exit(1);
        }
    }
    while ((p % 4) != 3)
    {
        fgetc(stream);
        p += 1;
    }
}

void skipstring(FILE *stream)
{
    unsigned int p = 0;
    char c;
    while (1)
    {
        c = fgetc(stream);
        if (c == '\0')
        {
            break;
        }
        p += 1;
    }
    while ((p % 4) != 3)
    {
        fgetc(stream);
        p += 1;
    }
}


