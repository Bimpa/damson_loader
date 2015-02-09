/**
 * @file spiNN_runtime.h
 * @details SPINNAKER Host side API
 * @author Paul Richmond (p.richmond@sheffield.ac.uk) - Copyright (c) 2011, University of Sheffield
 * @date 2011
 */
#ifndef SPINNAKER_RUNTIME_H_
#define SPINNAKER_RUNTIME_H_


#define SPINN_SUCCESS 1	//!< Successful function return value
#define SPINN_FAILURE 0 //!< Unsuccessful function return value. Use spiNN_get_error() to return the last error code.

#define MAX_VIRTUAL_PORTS 7
#define MAX_CORES_PER_CHIP 18

/**
 * Defines the error code which may be reported when using the SpiNN Host API.
 */
typedef enum
 {
	SPINN_NO_ERROR,                              /** Default error value indicating no error has occurred at runtime.
												   */
	SPINN_ERROR_CONNECTION_SOCKET_CREATION,    	 /** Error may be raised by the spiNN_init() function indicating that the communication
												   * socket could not be created.
												   */
	SPINN_ERROR_BOOT_SOCKET_CREATION, 			 /** Error may be raised by the spiNN_init() function indicating that the boot
	   	   	   	   	   	   	   	   	   	   	   	   * socket could not be created. This is needed to load the boot image (SCAMP).
	   	   	   	   	   	   	   	   	   	   	   	   */
	SPINN_ERROR_BOOT_FILE_NOT_FOUND,             /** Error may be raised by the spiNN_init() function indicating that the boot
   	   	   	   	   	   	   	   	   	   	   	   	   * file "boot.bin" could not be found.
   	   	   	   	   	   	   	   	   	   	   	   	   */
	SPINN_ERROR_BOOT_FILE_TOO_LARGE, 			 /** Error may be raised by the spiNN_init() function indicating that the boot
	   	   	   	   	   	   	   	   	   	   	   	   * file "boot.bin" size is too large.
	   	   	   	   	   	   	   	   	   	   	   	   */
	SPINN_ERROR_BOOT_FILE_READING, 				 /** Error may be raised by the spiNN_init() function indicating that the boot
	 	 	 	 	 	 	 	 	 	 	 	  * file "boot.bin" could not be read.
	 	 	 	 	 	 	 	 	 	 	 	  */
	SPINN_ERROR_CONNECTION_SERVER_ADDRESS,       /** Error may be raised by spiNN_init() function indicating that the device_ip address
												   * provided is incorrectly formatted.
	                                               */
	SPINN_ERROR_CONNECTION_DEBUG_SOCKET_CREATION,/** Error may be raised by the spiNN_init() function indicating that the debug
	 	 	 	 	 	 	 	 	 	 	 	   * socket could not be created.
	 	 	 	 	 	 	 	 	 	 	 	   */
	SPINN_ERROR_CONNECTION_DEBUG_SOCKET_BIND,    /** Error may be raised by the spiNN_init() function indicating that the debug
	 	 	 	 	 	 	 	 	 	 	 	   * socket could not be bound on port 17892.
	 	 	 	 	 	 	 	 	 	 	 	   */
	SPINN_ERROR_DEBUG_LISTENER_RECEIVE,          /** Error may be raised when a debug message is received function indicating that data
	 	 	   	   	   	   	   	   	   	   	   	   * could not be received by the socket.
	 	 	   	   	   	   	   	   	   	   	   	   */
	SPINN_ERROR_SDP_DATA_SIZE,                   /** Error may be raised by the spiNN_send_SDP_message() or spiNN_receive_SDP_message() functions
												   * indicating that the designated message length is beyond the 256 byte maximum.
												   */
	SPINN_ERROR_SDP_SEND,                        /** Error may be raised by spiNN_send_SDP_message(). Error indicates that the message or command
												   * could not be sent to the SpiNNaker hardware.
												   */
	SPINN_ERROR_SDP_RECEIVE,                     /** Error may be raised by the spiNN_receive_SDP_message() function. Error indicates that
	 	 	 	 	 	 	 	 	 	 	 	   * message data could not be received.
												   */
	SPINN_ERROR_SDP_TIMEOUT,                     /** Error may be raised by the spiNN_receive_SDP_message() function. Error indicates that the system timed
												   * out trying the read a command response or outgoing SDP message from the device.
	 	 	   	   	   	   	   	   	   	   	   	   */
	SPINN_ERROR_SDP_CMD_SEND,                    /** Error may be raised by most API functions which require communication with the host.
	 	 	 	 	 	 	 	 	 	 	 	   * Error indicates that the message or command could not be sent to the
	 	 	 	 	 	 	 	 	 	 	 	   * SpiNNaker hardware.
												   */
	SPINN_ERROR_SDP_CMD_RECEIVE,                 /** Error may be raised by most API functions which require communication with the host.
	 	 	 	 	 	 	 	 	 	 	 	   * Error indicates that message data could not be received.
												   */
	SPINN_ERROR_SDP_CMD_TIMEOUT,                 /** Error may be raised by most API functions which require communication with the host.
	 	 	   	   	   	   	   	   	   	   	   	   * Error indicates that the system timed out trying the read a command
	 	 	   	   	   	   	   	   	   	   	   	   * response or outgoing SDP message from the device. Usually raised by spiNN_init() if the device IP is
	 	 	   	   	   	   	   	   	   	   	   	   * valid but incorrect (i.e. device not on specified address).
	 	 	   	   	   	   	   	   	   	   	   	   */
	SPINN_ERROR_BOOT_PKT_SEND,					 /** Error may be raised by the spiNN_init() function indicating that there was a problem
	 	 	   	   	   	   	   	   	   	   	   	   * sending the boot image to the SpiNNaker hardware.
	 	 	   	   	   	   	   	   	   	   	   	   */
	SPINN_ERROR_BOOT_PKT_TIMEOUT, 				 /** Error may be raised by the spiNN_init() function indicating that the device timed out
	   	   	   	   	   	   	   	   	   	   	   	   * sending the boot image to the SpiNNaker hardware.
	   	   	   	   	   	   	   	   	   	   	   	   */
	SPINN_ERROR_LOAD_FILE_OPEN,                  /** Error may be raised by the spiNN_load_application() function. Error is the result of not being able to
												   * find or read the file specified.
												   */
	SPINN_ERROR_START_APP_ON_MONITOR,            /** Error may be raised by the spiNN_start_application() function. Error is raised if the user attempts to
												   * start an application on a virtually monitor core address (i.e with a core id if 0).
												   */
	SPINN_ERROR_SDP_VIRTUAL_PORT_RANGE,			 /** Error may be raised by the spiNN_send_SDP_message() function. Error is raised if the virtual port range
	                                               * is not within the range of 1-MAX_VIRTUAL_PORTS (0 is reserved).
	                                               */
	SPINN_ERROR_ADDRESS_CORE_ID					 /** Error may be raised by any function accepting a SpiNN_address argument. Error is raised if the core_id value
     	 	 	 	 	 	 	 	 	 	 	  * is not within the range of [0-(MAX_CORES_PER_CHIP-1)].
     	 	 	 	 	 	 	 	 	 	 	  */

 } spiNN_error;

/**
 * Represents a SpiNNaker virtualised core number.
 */
 typedef struct
 {
 	unsigned char x;		//!< chip x position.
 	unsigned char y;		//!< chip y position.
 	unsigned char core_id;	//!< core id
 } SpiNN_address;

 /**
  * Represents a SpiNNaker virtualised processor (chip) number.
  */
 typedef struct
 {
 	unsigned char x;		//!< chip x position.
 	unsigned char y;		//!< chip y position.
 } SpiNN_chip_address;


/**
  * @brief Connects the SpiNNaker device and performs system initialisation.
  *
  * This is the first API function which should be called and will perform the basic setting up of the communication
  * protocol for communicating with the SpiNNaker hardware. A socket for communication with the hardware will first
  * created (this will raise an SPINN_ERROR_CONNECTION_SOCKET_CREATION error if unsuccessful). The connection and SpiNNaker
  * hardware will then be tested by calling the spiNN_test_connection(). A debug socket will then be created and will
  * receive incoming debug messages via a separate thread. Finally the SpiNNaker device is configured to route debug output
  * back to the host (i.e. the IP of SpiNNaker host API application) and the system point-to-point communication is
  * Initialised using the x and y dimensions which describe the virtual chip layout.
  *
  * @param device_ip		The IP address of the SpiNNaker hardware device.
  * @param x_dimension		The X dimension of the virtual chip layout.
  * @param y_dimension		The Y dimension of the virtual chip layout.
  *
  * @return 				Returns SPINN_SUCCESS if a connection with the SpiNN board was successful and if the debug
  * message listener was created successfully. SPINN_FAILURE if any errors where raised.
  */
 int spiNN_init(char* device_ip, int x_dimension, int y_dimension);


/**
  * @brief Connects the SpiNNaker device and performs system initialisation on a specific port number.
  *
  * Usage is the same as spiNN_init() with an additional port option.
  *
  * @param device_ip		The IP address of the SpiNNaker hardware device.
  * @param x_dimension		The X dimension of the virtual chip layout.
  * @param y_dimension		The Y dimension of the virtual chip layout.
  * @param port				The port number to be used for SpiNNaker communication commands.
  *
  * @return 				Returns SPINN_SUCCESS if a connection with the SpiNN board was successful and if the
  * debug message listener was created successfully. SPINN_FAILURE if any errors where raised.
  */
int spiNN_init_port(char* device_ip, int x_dimension, int y_dimension, unsigned int port);


/**
 * @brief  Tests the connection to the SpiNNaker device.
 *
 * This function manually tests the connection to the SpiNNake hardware by querying the yBoot version number. spiNN_init()
 * must have already been called to establish a connection. If the connection test is successful the console will indicate
 * that the SpiNNaker host API program is connected to the hardware.
 *
 * @return 					Returns SPINN_SUCCESS if the yBoot version number was reported by the board successfully.
 * SPINN_FAILURE is returned if any errors where raised in sending or receiving commands to the board.
 */
int spiNN_test_connection();

/**
 * @brief Cleanup function to be used before exiting the SpiNNaker Host API program.
 *
 * Closes the connection, terminates debug threads and frees any allocated memory.
 */
void spiNN_exit();


/**
 * @brief Starts the application processor core at the given address.
 *
 * This performs an APLX command for the given core implying that the program must already have been loaded via spiNN_load_application().
 *
 * @param address 			The virtual core address to start the loaded program.
 *
 * @return					Returns SPINN_SUCCESS if the APLX command was successful SPINN_FAILURE otherwise. If the user attempts
 * to start a program on the monitor core (i.e. core_id = 0) then a SPINN_ERROR_START_APP_ON_MONITOR error will be raised and SPINN_FAILURE
 * will be returned.
 */
int spiNN_start_application(SpiNN_address address);

/**
 * @brief Starts the application processor core execution from specified device address at the given spiNNaker address.
 *
 * This performs an APLX command for the given core implying that the program must already have been loaded via spiNN_load_application().
 *
 * @param address 			The virtual core address to start the loaded program.
 * @param device_address 	The address in memory to start the APLX command.
 *
 * @return					Returns SPINN_SUCCESS if the APLX command was successful SPINN_FAILURE otherwise. If the user attempts
 * to start a program on the monitor core (i.e. core_id = 0) then a SPINN_ERROR_START_APP_ON_MONITOR error will be raised and SPINN_FAILURE
 * will be returned.
 */
int spiNN_start_application_at(SpiNN_address address, unsigned int device_address);



/**
 * @brief Loads an application to System RAM at the given chip address.
 *
 * The application file specified will be read and loaded to System RAM at the given chip address by writing it to the device in
 * small data chunks. If it is not possible to open the specified file a SPINN_ERROR_LOAD_FILE_OPEN will be raised and the function
 * will return SPINN_FAILURE.
 *
 * @param chip 				The SpiNNaker virtual chip address to load the application.
 * @param filename 			The application file to be loaded onto the device.
 *
 * @return 					If the file was successfully read and loaded without any problems then SPINN_SUCCESS will be returned
 * otherwise SPINN_FAILRE will be returned.
 */
int spiNN_load_application(SpiNN_chip_address chip, char* filename);


/**
 * @brief Loads an application to a given dice address at the given chip address.
 *
 * The application file specified will be read and loaded to the supplied device address at the given chip address by writing it to
 * the device in small data chunks. If it is not possible to open the specified file a SPINN_ERROR_LOAD_FILE_OPEN will be raised
 * and the function will return SPINN_FAILURE.
 *
 * @param chip 				The SpiNNaker virtual chip address to load the application.
 * @param filename 			The application file to be loaded onto the device.
 * @param device_address	The device address to write the file contents to
 *
 * @return 					If the file was successfully read and loaded without any problems then SPINN_SUCCESS will be returned
 * otherwise SPINN_FAILRE will be returned.
 */
int spiNN_load_application_at(SpiNN_address address, char* filename, unsigned int device_Address);

/**
 * @brief Read 'size' bytes of SpiNNaker memory at given address.
 *
 * 'size' bytes of memory are read in chunks from the SpiNNaker device at the given virtual core address and runtime memory device_address
 *  location. The resulting data is copied into host memory at the 'host_pointer' location.
 *
 * @param address 			The SpiNNaker virtual core address to read memory from.
 * @param host_destination 	The host destination to store data read from the device.
 * @param device_address 	The device runtime memory address.
 * @param size 				The size of data to read (bytes).
 *
 * @return If no errors were raised reading memory then SPINN_SUCCESS is returned otherwise SPINN_FAILURE is returned.
 */
int spiNN_read_memory(SpiNN_address address, char* host_destination, unsigned int device_address, unsigned int size);

/**
 * @brief Writes 'size' bytes of SpiNNaker memory at given address.
 *
 * 'size' bytes of memory are written in chunks to the SpiNNaker device at the given virtual core address and runtime memory device_address
 *  location. The data written is copied from host memory at the 'host_pointer' location.
 *
 * @param address 			The SpiNNaker virtual core address to write memory to.
 * @param host_destination 	The host destination where data is copied form to the device.
 * @param device_address 	The device runtime memory address.
 * @param size 				The size of data to write (bytes).
 *
 * @return If no errors were raised writing memory then SPINN_SUCCESS is returned otherwise SPINN_FAILURE is returned.
 * */
int spiNN_write_memory(SpiNN_address address, char* host_destination,  unsigned int device_address, unsigned int size);

/**
 * @brief Writes 'size' bytes of SpiNNaker memory at given address.
 *
 * 'size' bytes of memory are written in chunks to the SpiNNaker device at the given virtual core address and runtime memory device_address
 *  location. Only non zero values are copied. The data written is copied from host memory at the 'host_pointer' location.
 *
 * @param address 			The SpiNNaker virtual core address to write memory to.
 * @param host_destination 	The host destination where data is copied form to the device.
 * @param device_address 	The device runtime memory address.
 * @param size 				The size of data to write (bytes).
 *
 * @return If no errors were raised writing memory then SPINN_SUCCESS is returned otherwise SPINN_FAILURE is returned.
 * */
int spiNN_writenonzero_memory(SpiNN_address address, char* host_destination,  unsigned int device_address, unsigned int size);


/**
 * @brief Sends a SDP message into the system.
 *
 * Sends the 'message' data of length 'message_len' into the system at the given virtual core address as an SpiNNaker
 * Datagram Packet (SDP) message. If message_len is greater than 256 then the function will raise a SPINN_ERROR_SDP_DATA_SIZE
 * error and returns SPINN_FAILURE.
 *
 * @param address 			The SpiNNaker virtual core address to send the SDP message.
 * @param virtual_port		Virtual port number to send SDP message via range of 1-MAX_VIRTUAL_PORTS]. 0 is reserved.
 * @param message 			Pointer to data to be contained within the SDP message.
 * @param message_len		Size of the data to send (bytes)
 *
 * @return					If the message is sent without raising any errors then SPINN_SUCCESS is returned otherwise
 * SPINN_FAILURE is returned.
 */
int spiNN_send_SDP_message(SpiNN_address address, char virtual_port, char* message, unsigned int message_len);

/**
 * @brief Receives an SDP message into the system.
 *
 * Receives a SpiNNaker Datagram Packet (SDP) message of length 'message_len' from the device placing into the memory pointed
 * to by 'message' argument. The originating virtual core address is returned in the SpiNN_address pointed to by the 'from'
 * argument. This function will block the main program execution until a message is either returned or the message read times
 * out. In the case of a timeout a SPINN_ERROR_SDP_TIMEOUT error will be raised an the function will return SPINN_FAILURE. If
 * message_len is greater than 256 then the function will raise a SPINN_ERROR_SDP_DATA_SIZE error and returns SPINN_FAILURE.
 *
 * @param source			Pointer to a SpiNN_address structure to hold the originating message address.
 * @param virtual_port		Pointer to a char which will be set to the virtual port number to SDP message was received via. Range of 1-MAX_VIRTUAL_PORTS]. 0 is reserved.
 * @param message			Pointer to memory to store the incoming message data.
 * @param message_len		Maximum size of the data to receive (bytes).
 * @return					If the message is received without raising any errors then SPINN_SUCCESS is returned otherwise
 * SPINN_FAILURE is returned.
 */
int spiNN_receive_SDP_message(SpiNN_address* source, char* virtual_port, char* message, int message_len);

/**
 * @brief Sets the Debug (device stdout) message handler to the function pointer supplied which accepts a message as an argument.
 *
 * The host side API polls for messages in a separate thread and calls the assigned message handler when a message is received.
 * By default the spiNN_handle_debug_message() message handler is assigned.
 *
 * @param receive_message 	Pointer to a function which accepts a SpiNN_address and char* as an argument to handle debug messages.
 * See spiNN_handle_debug_message() for an example.
 */
void spiNN_debug_message_callback(void (*receive_message)(SpiNN_address, char*));

/**
 * @brief Default message handler for debug messages.
 *
 * The default message handler for text formatted debug messages. Prints formatted message to the console.
 *
 * @param address			The virtual core address of the incoming debug message.
 * @param message			The string of message data of the incoming message.
 */
void spiNN_handle_debug_message(SpiNN_address address, char* message);


/**
 * @brief Sets an error callback function which is called if an error occurs at runtime.
 *
 * The specified error callback function is called whenever an error occurs at runtime within the SpiNNaker Host API. The default error
 * callback is spiNN_print_error().
 *
 * @param error_callback	Pointer to an error callback function (accepting no arguments) which is used to handle errors at runtime.
 * Default is spiNN_print_error().
 */
void spiNN_set_error_callback(void (*error_callback)(void));

/**
 * @brief Gets the last known error code.
 *
 * Returns the last known error code raised. If no error has occurred then SPINN_NO_ERROR will be returned.
 *
 * @return 					The last known error code. SPINN_NO_ERROR if no errors.
 */
spiNN_error spiNN_get_error();

/**
 * @brief Gets the error string description given an error code.
 *
 * Converts an error code returned by spiNN_get_error() into a detailed error description.
 *
 * @param error				An error code to be converted into a string description.
 *
 * @return					A detailed error description of the given error code.
 */
const char* spiNN_get_error_string(spiNN_error error);

/**
 * @brief Default error callback.
 *
 * Default error callback prints the error code and string description to the console.
 */

void spiNN_print_error(void);

/*
 * Functions suggested by Manchester
 * void spiNN_read_system_time();
 * void spiNN_read_application_time();
 * void spiNN_pause_application(unsigned int at_system_time);
 * void spiNN_continue_application(unsigned int at_system_time);
 * void spiNN_stop_application(unsigned int at_system_time);
 * void Write_routeing_table(+++);
 * void Read_routeing_table(+++);
 * void Start_stats_gathering(various, +++);
 * void Stop_stats_gathering(various, +++);
 * void Read_stats(various, +++);
 * void Reset_stats(various, +++);
 * void Monitoring(interval);			???
*/


#endif /* SPINNAKER_RUNTIME_H_ */
