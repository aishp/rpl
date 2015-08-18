/* GLOBAL VALUES
DIO
DIS
TFLAG
dismflag: 1 if DIS is still being multicast, 0 if dismcast_socket has been closed
parent_table
routing_table
disrecvport = 49152 (listens to DIS) - disrecv_callback
diorecvport = 49153 (listens to DIO) - diorecv_callback
dismcastport = 49154 (multicasts DIS) - dismcast_callback
diobcastport = 49155 (Broadcasts DIO) - diobcast_callback
disrecv_sock
dismcast_sock
diobcast_sock
diorecv_sock
rpl_float_func()
mcast_dis()
bcast_dis()
trickle_timer()
dio_init()
dis_init()
1. disrecv_callback()
	if (floating) 
	  return
	else
	  start trickle timer (if tflag not set)
	  unicast back DIO
        
DIO Message Fields
	uint16_t rank = 1; //16 bit rank
	int grounded; //1 bit flag indicationg of node if grounded (1) or floating(0)
	uint16_t *dodag_id; //128 bit IPV6 address
	uint8_t etx; //estimated hops left, 8 bit
	uint16_t nid; //self node id
	uint8_t version;
*/

/* TO DO
dio_ack: reset in dio_ack socket
*/


#include<libmsgpack.c>
#include<time.h>
#include<stdlib.h>

#define RPL_SYMBOLS \
	{ LSTRKEY("root_func"), LFUNCVAL(rpl_root_func)}, \
	{ LSTRKEY("float_func"), LFUNCVAL(rpl_float_func)}, \
	
#define disrecvport 49152 
#define diorecvport 49153
#define dismcastport 49154
#define diobcastport 49155

int dismcast_callback(lua_State *L);
int diobcast_callback(lua_State *L);
int diorecv_callback(lua_State *L);
int disrecv_callback(lua_State *L);

int t_timeout(lua_State *L);

//(rank, gf, dodag_id, etx, version)->nil
int dio_init(lua_State *L)
{
	//DIO = { Rank, G/F, Dodag ID, ETX, Node ID, Version }
	
	lua_getglobal(L, "DIO");
	int table_index=lua_gettop(L);

	lua_pushstring(L, "rank");
	lua_pushvalue(L, lua_tonumber(L, 1)+1); //parent rank is the first parameter
	lua_settable(L, table_index);

	lua_pushstring(L, "GF");
	lua_tonumber(L, 2);
	lua_settable(L, table_index);

	lua_pushstring(L, "dodag_id");
	lua_pushvalue(L, 3);
	lua_settable(L, table_index);

	lua_pushstring(L, "etx"); //same as rank?
	lua_pushvalue(L, 4);
	lua_settable(L, table_index);

	lua_pushstring(L, "node_id");
	lua_pushlightfunction(L, libstorm_os_getnodeid);	
	lua_call(L, 0, 1);
	lua_settable(L, table_index);

	lua_pushstring(L, "version");
	lua_pushvalue(L, 5); 
	lua_settable(L, table_index);

	//following line might not be necessary:
	//lua_pushvalue(L, table_index);

	lua_setglobal(L, "DIO");
	
	return 0;
}

int dis_init(lua_State *L)
{
	//DIS = {Node ID}

	lua_createtable(L, 0, 2);
	int table_index=lua_gettop(L);

	lua_pushstring(L, "node_id");
	lua_pushlightfunction(L, libstorm_os_getnodeid);	
	lua_call(L, 0, 1);
	lua_settable(L, table_index);

	lua_setglobal(L, "DIS");
	return 0;
}

int bcast_dio(lua_State *L)
{
	//libstorm_net_sendto(socket, payload, ip, port)
	//multicast address: "ff02::1"
	lua_pushlightfunction(L, libstorm_net_sendto);
	lua_getglobal(L, "diobcast_sock");
	lua_pushlightfunction(L, libmsgpack_mp_pack);
	lua_getglobal(L, "DIO");
	lua_call(L,1,1);
	lua_pushstring(L, "ff02::1");
	lua_pushnumber(L, diorecvport);
	lua_call(L, 4, 1);
	return 0;
}

int mcast_dis(lua_State *L)
{
	lua_pushlightfunction(L, libstorm_net_sendto);
	lua_getglobal(L, "dismcast_sock");
	lua_pushlightfunction(L, libmsgpack_mp_pack);
	lua_getglobal(L, "DIS");
	lua_call(L,1,1);
	lua_pushstring(L, "ff02::1");
	lua_pushnumber(L, disrecvport);
	lua_call(L, 4, 1);
	
	lua_getglobal(L, "dismflag");
	if(lua_tonumber(L, -1)==1)
	{
		cord_set_continuation(L, mcast_dis, 0);
		return nc_invoke_sleep(L, 5 * SECOND_TICKS); //can be changed
	}
	else
	{
		return cord_return(L, 0);
	}
}
//args: i, inst
//returns: 0 on oldinstance of trickle timer
int i_timeout(lua_State *L)
{
	int t, imin, imax;
	int i= lua_tonumber(L, lua_upvalueindex(1));
	int l_inst= lua_tonumber(L, lua_upvalueindex(2)); //local instance number
	
	srand(time(NULL)); 
	
	//first check if the local trickle instance is same as global instance. if not- terminate
	lua_getglobal(L, "TRICKLE_INSTANCE");
	int g_inst = lua_tonumber(L, -1);
	if(g_inst!= l_inst)
	{
		//new instance of trickle timer has been started, this one must be stopped
		return cord_return(L, 0);
	}
	else
	{
		//i timed out, double i
		lua_getglobal(L, "IMIN");
		imin = lua_tonumber(L, -1);
		lua_getglobal(L, "IMAX");
		imax = lua_tonumber(L, -1);
		
		if((2*i)<(imin*(2^imax)))
		{
			i*=2;
		}	
	
		else
		{
			i = imin * (2^imax);
		}
		
		//get new value of t
		t = (rand() % ((i)-(i/2))) + (i/2) + 1;
		
		lua_pushnumber(L, t);
		lua_pushnumber(L, i);
		lua_pushnumber(L, l_inst); 
		cord_set_continuation(L, t_timeout, 3);
		
		//call t_timeout after t ticks
		return nc_invoke_sleep(L, (t) * SECOND_TICKS);
	}
}

int t_timeout(lua_State *L)
{
	//function invoked after t ticks, time to check if c<k and transmit

	int t= lua_tonumber(L, lua_upvalueindex(1));
	int i= lua_tonumber(L, lua_upvalueindex(2));
	int l_inst= lua_tonumber(L, lua_upvalueindex(3)); //local instance number
	
	
	//first check if the current trickle instance is same as instance running
	//global instance
	lua_getglobal(L, "TRICKLE_INSTANCE");
	int g_inst = lua_tonumber(L, -1);
	if(g_inst!= l_inst)
	{
		//new instance of trickle timer has been started, this one must be stopped
		return cord_return(L, 0);
	}
	else
	{
		lua_getglobal(L, "C");
		lua_getglobal(L, "k");
		if(lua_tonumber(L, -2)<lua_tonumber(L,-1)) //if(c<k)
		{
			//transmit
			lua_pushlightfunction(L, bcast_dio);
			lua_call(L, 0, 0);
		}
	
		
		lua_pushnumber(L, i);
		lua_pushnumber(L, l_inst); 
		cord_set_continuation(L, i_timeout, 2);
		
		//call i_timeout after (i-t) ticks
		return nc_invoke_sleep(L, (i-t) * SECOND_TICKS);
	}
}

int trickle_timer(lua_State *L)
{
	int imin, imax,i,t;
	
	//initialize random number generator
	srand(time(NULL)); 
	
	lua_getglobal(L, "IMIN");
	imin = lua_tonumber(L, -1);
	lua_getglobal(L, "IMAX");
	imax = lua_tonumber(L, -1);
	
	//current interval size, random number between imin and imax
 	i = (rand() % ((imin* (2^imax))-imin)) + 1; 
	
	//counter	
	int c=0; 
	lua_pushnumber(L, c);
	lua_setglobal(L, "C");
	
	// a time within the current interval, random number between (i/2) and i
	t = (rand() % ((i)-(i/2))) + (i/2) + 1;
	
	//create t timeout flag
	lua_pushnumber(L, 0);
	lua_setglobal(L, "t_timeout_flag");

	//create i timeout flag
	lua_setglobal(L, "i_timeout_flag");

	//socket for listening to data: eport 49154
	//disdis_callback(): received DIO, changes the value of "c"

	//start first interval 
	//trickle_continuation(t, i, inst)->0 (keeps running)
	
	lua_pushnumber(L, t);
	lua_pushnumber(L, i);
	lua_getglobal(L, "TRICKLE_INSTANCE"); 
	cord_set_continuation(L, t_timeout, 3);
	return nc_invoke_sleep(L, t * SECOND_TICKS);
	
}

//actions to be performed upon receipt of msg on DIO bcast socket
int diobcast_callback(lua_State *L)
{
	char *pay= (char *)lua_tostring(L, 1);
	char *srcip=(char *)lua_tostring(L, 2);
	uint32_t srcport = lua_tonumber(L, 3);
	printf("Received %s from %s on port %u (DIS mcast)\n", pay, srcip, (unsigned int)srcport);
	
	return 0;
}

//actions to be performed upon receipt of msg on DIS mcast socket
int dismcast_callback(lua_State *L)
{
	char *pay= (char *)lua_tostring(L, 1);
	char *srcip=(char *)lua_tostring(L, 2);
	uint32_t srcport = lua_tonumber(L, 3);
	
	printf("Received %s from %s on port %u (DIS mcast)\n", pay, srcip, (unsigned int)srcport);
	
	return 0;
}

//for DIO broadcast according to trickle timer
int create_diobcast_socket(lua_State *L)
{
	lua_pushlightfunction(L, libstorm_net_udpsocket);
	lua_pushnumber(L, diobcastport);
	lua_pushlightfunction(L, diobcast_callback);
	lua_call(L,2,1);
	lua_setglobal(L, "diobcast_sock");
	return 0;
}

//create socket for receiving dis messages
int create_disrecv_socket(lua_State *L)
{
	lua_pushlightfunction(L, libstorm_net_udpsocket);
	lua_pushnumber(L, disrecvport);
	lua_pushlightfunction(L, disrecv_callback);
	lua_call(L,2,1);
	lua_setglobal(L, "disrecv_sock");
	return 0;
}

//for multicasting DIS 
int create_dismcast_socket(lua_State *L)
{
	lua_pushlightfunction(L, libstorm_net_udpsocket);
	lua_pushnumber(L, dismcastport);
	lua_pushlightfunction(L, dismcast_callback);
	lua_call(L,2,1);
	lua_setglobal(L, "dismcast_sock");
	return 0;
}

//for receiving DIO broadcast 
int create_diorecv_socket(lua_State *L)
{
	lua_pushlightfunction(L, libstorm_net_udpsocket);
	lua_pushnumber(L, diorecvport);
	lua_pushlightfunction(L, diorecv_callback);
	lua_call(L,2,1);
	lua_setglobal(L, "diorecv_sock");
	return 0;
}



//(Payload, srcip, srcport)
//function called when dio msg received on eport
//actions to perform upon receipt of dio

int diorecv_callback(lua_State *L)
{
	lua_pushlightfunction(L, libmsgpack_mp_unpack);
	lua_pushvalue(L, 1);
	lua_call(L, 1, 1);
	int tab_index=lua_gettop(L); //DIO received
	char *srcip = (char *)lua_tostring(L, 2);
	
	//stop DIS multicast
	lua_getglobal(L, "dismflag"); //flag that checks if dis is still being multicast
	if(lua_tonumber(L, -1)==1)
	{
		lua_pushlightfunction(L, libstorm_net_close);
		lua_getglobal(L, "dismcast_sock");
		lua_call(L, 1, 0);
		lua_pushnumber(L, 0);
		lua_setglobal(L, "dismflag");
	}
	
	lua_getglobal(L, "DIO");
	int self_dio= lua_gettop(L); 
	
	lua_pushstring(L, "rank");
	lua_gettable(L, self_dio);
	
	if(lua_tonumber(L, -1)>1) // not a root node
	{
		int s_rank = lua_tonumber(L, -1);
		
		//add to parent list
		lua_getglobal(L, "parent_table");
		lua_pushstring(L, "node_id");
		lua_gettable(L, tab_index);
		lua_pushstring(L, srcip);
		lua_settable(L, -3);
		lua_setglobal(L, "parent_table");
		
		lua_pushstring(L, "rank");
		lua_gettable(L, tab_index);
		int p_rank = lua_tonumber(L, -1);
		
		if((s_rank==-1) || (s_rank > (p_rank+1)))
		{
			//set preferred parent as incoming DIO, which is on top of the stack
			lua_pushvalue(L, tab_index);
			lua_setglobal(L, "PrefParent");
			lua_pop(L, 1);
			
			//Update self dio with new rank, gf, dodag_id, etx and version (from parent)
			lua_pushlightfunction(L, dio_init);
			lua_pushstring(L, "rank");
			lua_pushnumber(L, lua_tonumber(L, -1)+1);//push new rank
			lua_pushnumber(L, 1);//GF=1
			lua_pushstring(L, "dodag_id");
			lua_gettable(L, tab_index);//same dodag_id as parent
			lua_pushstring(L, "etx");
			lua_gettable(L, tab_index);
			int etx=lua_tonumber(L, -1);
			lua_pop(L, 1);
			lua_pushnumber(L,etx+1);//etx of parent+1 (for now,later add another measure for calculating etx)
			lua_pushstring(L, "version");
			lua_gettable(L, tab_index);//same dodag_version as parent
			lua_call(L, 5, 0);
	
			//inconsistent state reached, reset everything and start trickle timer again
			//**STOP THE OLD INSTANCE OF TRICKLE TIMER**
			lua_getglobal(L, "TRICKLE_INSTANCE");
			lua_pushnumber(L, lua_tonumber(L, -1)+1);
			lua_setglobal(L, "TRICKLE_INSTANCE");
			lua_pushlightfunction(L, trickle_timer);
			lua_call(L, 0, 0);
		}
	
		else
		{
			//consistent state, increment C(trickle timer counter)
			lua_getglobal(L, "C");
			lua_pushnumber(L, lua_tonumber(L, -1)+1);
			lua_setglobal(L, "C");
		}	
	}
	
	return 0;
}

//actions to perform upon receipt of dis	
int disrecv_callback(lua_State *L)
{
	//if floating, don't respond with DIO
	lua_getglobal(L, "DIO");
	lua_pushstring(L, "grounded");
	lua_gettable(L, -2);
	if(lua_tonumber(L, -1)==0)
	{
		return 0;
	}
	//get DIS msg from payload
	

	char *srcip = (char *)lua_tostring(L, 2);
	
/*	//Should I unpack DIS??
	lua_pushlightfunction(L, libmsgpack_mp_unpack);
	lua_pushvalue(L, 1);
	lua_call(L, 1, 1);
*/

	//Unicast back DIO
	lua_pushlightfunction(L, libstorm_net_sendto);
	lua_getglobal(L, "disrecv_sock");
	lua_pushlightfunction(L, libmsgpack_mp_pack);
	lua_getglobal(L, "DIO");
	lua_call(L, 1, 1);
	lua_pushstring(L, srcip);
	lua_pushnumber(L, diorecvport);
	lua_call(L,4,1);
	
	lua_getglobal(L, "TFLAG");
	if(lua_tonumber(L, -1)==0)
	{
		//set TFLAG to show that trickle timer is currently running
		lua_pushnumber(L, 1);
		lua_setglobal(L, "TFLAG");
		
		//call the trickle timer, let it run concurrently in the background
		lua_pushlightfunction(L, trickle_timer);
		lua_call(L, 0, 0);
	}

	return 0;
}

//actions that take place in a root node
int rpl_root_func(lua_State *L)
{
	//socket for listening to DIS and responding with DIO unicast
	lua_pushlightfunction(L, create_disrecv_socket);
	lua_call(L, 0, 0);

	//socket for broadcasting DIO
	lua_pushlightfunction(L, create_diobcast_socket);
	lua_call(L, 0, 0);

	//initialize DIO table
	lua_pushlightfunction(L, dio_init);
	lua_pushnumber(L, 1); //rank of root
	lua_pushnumber(L, 1); //root is grounded
	lua_pushlightfunction(L, libstorm_os_getnodeid);
	lua_call(L, 0, 1);//dodag id is same as node id for root
	lua_pushnumber(L, 0); //etx is 0, directly connected to border router
	lua_pushnumber(L, 1);//version 1
	lua_call(L, 5, 0); 

	//initialize DIS table
	lua_pushlightfunction(L, dis_init);
	lua_call(L, 0, 0);
	
	//set TFLAg to 0 to show that trickle timer is not currently running
	lua_pushnumber(L, 0);
	lua_setglobal(L, "TFLAG");

	//Set trickle timer parameters
	//minimum interval size in seconds
	lua_pushnumber(L, 5);
	lua_setglobal(L, "IMIN");
		
	//number of doublings of imin	
	lua_pushnumber(L, 16);
	lua_setglobal(L, "IMAX");
	
	//number of consistent messages for no transmission
	lua_pushnumber(L, 1);
	lua_setglobal(L, "K");
		
	//set global trickle instance flag, to keep track whether we are running the right instance or an old instance
	lua_pushnumber(L, 0);
	lua_setglobal(L, "TRICKLE_INSTANCE"); 
	
	//set flag to show that DIS is not being multicast
	lua_pushnumber(L, 0);
	lua_setglobal(L, "dismflag");
	
	//set empty parent table
	lua_newtable(L);
	lua_setglobal(L, "parent_table");
	
	//set empty routing table
	lua_newtable(L);
	lua_setglobal(L, "routing_table");

	return 0;
	
}

int rpl_float_func(lua_State *L)
{
	//socket for listening to DIS and responding with DIO unicast
	lua_pushlightfunction(L, create_disrecv_socket);
	lua_call(L, 0, 0);

	//socket for multicasting DIS 
	lua_pushlightfunction(L, create_dismcast_socket);
	lua_call(L, 0, 0);
	
	//socket for receiving DIO unicast/ broadcast
	lua_pushlightfunction(L, create_diorecv_socket);
	lua_call(L, 0, 0);

	//socket for broadcasting DIO
	lua_pushlightfunction(L, create_diobcast_socket);
	lua_call(L, 0, 0);

	//initialize DIO table
	lua_createtable(L, 0, 6);
	lua_setglobal(L, "DIO");
	lua_pushlightfunction(L, dio_init);
	lua_pushnumber(L, -1); //rank of floating node
	lua_pushnumber(L, 0); //floating
	lua_pushnumber(L, -1); //not associated to any dodag, so dodag_id is -1
	lua_pushnumber(L, 10000); //infinite etx
	lua_pushnumber(L, -1);//no version yet since node is floating
	lua_call(L, 5, 0); 

	//initialize DIS table
	lua_pushlightfunction(L, dis_init);
	lua_call(L, 0, 0);

	//set flag to show that DIS is being multicast
	lua_pushnumber(L, 1);
	lua_setglobal(L, "dismflag");

	//start DIS multicast
	lua_pushlightfunction(L, mcast_dis);
	lua_call(L, 0, 0);

	//set TFLAg to 0 to show that trickle timer is not currently running
	lua_pushnumber(L, 0);
	lua_setglobal(L, "TFLAG");
	
	//Set trickle timer parameters
	//minimum interval size in seconds
	lua_pushnumber(L, 5);
	lua_setglobal(L, "IMIN");
		
	//number of doublings of imin	
	lua_pushnumber(L, 16);
	lua_setglobal(L, "IMAX");
	
	//number of consistent messages for no transmission
	lua_pushnumber(L, 1);
	lua_setglobal(L, "K");
		
	//set global trickle instance flag, to keep track whether we are running the right instance or an old instance
	lua_pushnumber(L, 0);
	lua_setglobal(L, "TRICKLE_INSTANCE"); 

	//set empty parent table
	lua_newtable(L);
	lua_setglobal(L, "parent_table");
	
	//set empty routing table
	lua_newtable(L);
	lua_setglobal(L, "routing_table");

	return 0;
}
