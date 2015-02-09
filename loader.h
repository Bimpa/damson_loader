#ifndef LOADER
#define LOADER

#define LOADER_DEBUG 		1
#define MAX_STRING_SIZE 	128

#include "damson_runtime.h"

// interrupt vector
typedef struct
{
    unsigned int  src_node;
    unsigned int  code_offset;
    unsigned int  count;	//number of times the interrupt occours
} InterruptVector;

//loader log item
typedef struct
{
	uint	handle;
	uint 	log_items;
	char 	format[MAX_STRING_SIZE];
	char 	filename[MAX_STRING_SIZE];
	FILE 	*outputfile;
} LoaderLogItem;

//node map item
typedef struct
{
	  unsigned int  damson_node_id;
	  unsigned int  num_interrupts;
	  unsigned int  *interrupts;
	  unsigned int  num_logs;
	  LoaderLogItem *logs;
	  unsigned int  num_snapshots;
	  LoaderLogItem *snapshots;
} NodeMapItem;

//runtime logitem
typedef struct
{
	uint	handle;
	uint	start_time;	//us
	uint 	end_time;	//us
	uint	interval;	//us
	int		interval_count;	//us
	uint 	log_items;
	int		log_globals[MAX_LOG_ITEMS];
} RuntimeLogItem;

/**
 * Initialise SpiNNaker for loading.
 * Mapping file contains PCB layout and damson node to core mappings
 */
void InitLoader();

/**
 * Exit SpiNNaker loading.
 * safe exit function
 */
void ExitLoader();

/*
 * Adds a node item map (i.e. a node number and interrupts) to the mapper
 */
void AddNodeMapItem(NodeMapItem* map);

/**
 * Creates the DAMSON node to SpiNNaker core maps
 */
void MapNodes();

/**
 * Initialises a SpiNNaker core and loads the prototype program into instruction memory
 * Intelligently load the gv, ev and interrupt vector
 */
void LoadNode(unsigned int    node,
			  char            *prototype_object_name,
			  int             *gv,       unsigned int gvusersize,
			  int             *ev,       unsigned int evsize,
			  InterruptVector *intv,     unsigned int intvsize,
			  RuntimeLogItem  *logs,     unsigned int num_logs,
			  RuntimeLogItem  *snapshots,unsigned int num_snapshots,
			  int debug_mode);

/**
 * Checks that the node info has been loaded to the appropriate area of memory on the SpiNNaker core. If any errors are found 0 is returned.
 */
int CheckNodeMemory(unsigned int    node,
				 int             *gv,       unsigned int gvusersize,
				 int             *ev,       unsigned int evsize,
				 InterruptVector *intv,     unsigned int intvsize,
				 RuntimeLogItem  *logs,     unsigned int num_logs,
				 RuntimeLogItem  *snapshots,unsigned int num_snapshots);

/**
 * Starts executing the code
 *
 * Blocking function begins waiting for output from DAMSON program.
 * Prints any debug output to the command line, saves any logging and returns once all cores have exited.
 * Returns after hardware simulation has completed
 */
void Start();





#endif //LOADER
