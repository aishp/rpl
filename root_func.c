/* GLOBAL VALUES

DIO
DIS
TFLAG
neighbor_table
routing_table

disport = 49152 (listens to DIS)
dioport = 49153 (listend to DIO)
disdioport = 49154 (multicasts DIS, received DIO unicast and broadcast)
*/

/* TO DO
dio_ack: reset in dio_ack socket

*/
#define RPL_SYMBOLS \
	{ LSTRKEY("ground_func"), LFUNCVAL(rpl_ground_func)}, \
	
#define disport 49152 
#define dioport 49153

//DIO Message Fields
struct dio
{
	uint16_t rank = 1; //16 bit rank
	int grounded; //1 bit flag indicationg of node if grounded (1) or floating(0)
	uint16_t *dodag_id; //128 bit IPV6 address
	uint8_t etx; //estimated hops left, 8 bit
	uint16_t nid; //self node id
	uint8_t version;
};

int dio_init_root(lua_State *L)
{
	//DIO = { Rank, G/F, Dodag ID, ETX, Node ID, Version }
	lua_createtable(L, 0, 6);
	int table_index=lua_gettop(L);
	lua_pushstring(L, "rank");
	lua_pushnumber(L, 1);
	lua_settable(L, table_index);
	lua_pushstring(L, "GF");
	lua_pushnumber(L, 1);
	lua_settable(L, table_index);
	lua_pushstring(L, "dodag_id");
	lua_pushnumber(L, 1); //find out how to assign
	lua_settable(L, table_index);
	lua_pushstring(L, "etx"); //same as rank?
	lua_pushnumber(L, 0);
	lua_settable(L, table_index);
	lua_pushstring(L, "node_id");
	lua_pushlightfunction(L, libstorm_os_getnodeid);	
	lua_call(L, 0, 1);
	lua_settable(L, table_index);
	lua_pushstring(L, "version");
	lua_pushnumber(L, 0); 
	lua_settable(L, table_index);
	//following line might not be necessary:
	//lua_pushvalue(L, table_index);
	lua_setglobal(L, "DIO");
	
	return 0;
}



int dis_init_root(lua_State *L)
{
	//DIS = {Rank, Node ID}

	lua_createtable(L, 0, 2);
	int table_index=lua_gettop(L);
	lua_pushstring(L, "rank");
	lua_pushnumber(L, 1);
	lua_settable(L, table_index);
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
	lua_getglobal(L, "dio_sock");
	lua_pushlightfunction(L, libstorm_mp_pack);
	lua_getglobal(L, "DIO");
	lua_call(L,1,1);
	lua_pushstring(L, "ff02::1");
	lua_pushnumber(L, dio_port);
	lua_call(L, 4, 1);
	return 0;
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
		return 0;
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
		lua_set_continuation(L, t_timeout, 3);
		
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
		return 0;
	}
	else
	{
		lua_getglobal(L, "C");
		c=lua_tonumber(L, -1);
		
		if(c<k)
		{
			//transmit
			lua_pushlightfunction(L, bcast_dio);
			lua_call(L, 0, 0);
		}
	
		
		lua_pushnumber(L, i);
		lua_pushnumber(L, l_inst); 
		lua_set_continuation(L, i_timeout, 2);
		
		//call i_timeout after (i-t) ticks
		return nc_invoke_sleep(L, (i-t) * SECOND_TICKS);
	}
}

int trickle_timer(lua_State *L)
{
	int k, imin, imax,i,t;
	
	//initialize random number generator
	srand(time(NULL)); 
	
	lua_getglobal(L, "IMIN");
	imin = lua_tonumber(L, -1);
	lua_getglobal(L, "IMAX");
	imax = lua_tonumber(L, -1);
	lua_getglobal(L, "K");
	k = lua_tonumber(L, -1);
	
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
	lua_set_continuation(L, t_timeout, 3);
	return nc_invoke_sleep(L, t * SECOND_TICKS);
	
}

//actions to be performed upon receipt of msg on DIO bcast socket
int diob_callback(lua_State *L)
{
	char *pay= (char *)lua_tostring(L, 1);
	char *srcip=(char *)lua_tostring(L, 2);
	uint32_t srcport = lua_tonumber(L, 3);
	
	printf("Received %s from %s on port %u\n", pay, srcip, srcport);
	return 0;
}
//actions to perform upon receipt of dis	
int dis_callback(lua_State *L)
{
	//get DIS msg from payload
	lua_pushlightfunction(L, libmsgpack_mp_unpack);
	lua_pushvalue(L, 1);
	lua_call(L, 1, 1);
	
	int tab_index=lua_gettop(L);

	char *srcip = lua_tostring(L, 2);
	uint32_t srcport = lua_tonumber(L, 3);
	
	//trickle timer parameters;
	int imin = 5; 
	int imax = 16; 
	int k = 1; 
	int tflag;
	
	
	int  rv=0; //return value from libstorm_net_sendto
	
	lua_getglobal(L, "neighbor_table"); 
	
	//EDIT line: Get nodeid from table
	lua_pushnumber(L, payload->dodag_id);// push node id
	
	lua_pushstring(L, srcip);
	lua_settable(L, -3);
	lua_setglobal(L, "neighbor_table");

//unicast back DIO
	do
	{
		lua_pushlightfunction(L, libstorm_net_sendto);
		lua_getglobal(L, "dis_sock");
		lua_pushlightfunction(L, libstorm_mp_pack);
		lua_getglobal(L, "DIO");
		lua_call(L, 1, 1);
		lua_pushstring(L, srcip);
		lua_pushnumber(L, disport);
		lua_call(L,4,1);
		lua_getglobal(L, dio_ack); //by default set it to 1, change later
	}while(lua_tonumber(L,-1)!=1);
	
	lua_getglobal(L, "TFLAG");
	tflag = lua_checknumber(L, -1);
	
	if(tflag==0)
	{
		//minimum interval size in seconds
		lua_pushnumber(L, imin);
		lua_setglobal(L, "IMIN");
		
		//number of doublings of imin	
		lua_pushnumber(L, imax);
		lua_setglobal(L, "IMAX");
	
		//number of consistent messages for no transmission
		lua_pushnumber(L, k);
		lua_setglobal(L, "K");
		
		//set TFLAG to show that trickle timer is now running
		lua_pushnumber(L, 1);
		lua_setlobal(L, "TFLAG");
		
		//set global trickle instance flag, to keep track whether we are running the right instance or an old instance
		lua_pushnumber(L, 0);
		lua_setglobal(L, "TRICKLE_INSTANCE"); 
		
		//call the trickle timer, let it run concurrently in the background
		lua_pushlightfunction(L, trickle_timer);
		lua_call(L, 0, 0);
	}
	      
}

//for DIO broadcast according to trickle timer
int create_dio_bsocket(lua_State *L)
{
	lua_pushlightfunction(L, libstorm_net_udpsocket);
	lua_pushnumber(dport);
	lua_pushlightfunction(dio_callback);
	lua_call(L,2,1);
	lua_setglobal(L, "dio_sock");
	return 0;
}

//create socket for receiving dis messages
int create_dis_socket(lua_State *L)
{
	
	lua_pushlightfunction(L, libstorm_net_udpsocket);
	lua_pushnumber(disport);
	lua_pushlightfunction(dis_callback);
	lua_call(L,2,1);
	lua_setglobal(L, "dis_sock");
	return 0;
}

//for multicasting DIS and receiving DIO unicast/broadcast
int create_disdio_socket(lua_State *L)
{
	lua_pushlightfunction(L, libstorm_net_udpsocket);
	lua_pushnumber(disdioport);
	lua_pushlightfunction(disdio_callback);
	lua_call(L,2,1);
	lua_setglobal(L, "disdio_sock");
	return 0;
}

//actions that take place in a root node
int ground_func(lua_State *L)
{
	//socket for listening to DIS and responding with DIO unicast
	lua_pushlightfunction(L, create_dis_socket);
	lua_call(L, 0, 0);

	//socket for DIO bcast according to trickle timer
	lua_pushlightfunction(L, create_dio_bsocket);
	lua_call(L, 0, 0);
	
	//socket for multicasting DIS and receiving DIO unicast (for DODAG creation) and DIO bcast (according to trickle timer)
	lua_pushlightfunction(L, create_disdio_socket);
	lua_call(L, 0, 0);

	//initialize DIO table
	lua_pushlightfunction(L, dio_init_root);
	lua_call(L, 0, 0);
	
	//initialize DIS table
	lua_pushlightfunction(L, dis_init_root);
	lua_call(L, 0, 0);
	
	//set TFLAg to 0 to show that trickle timer is not currently running
	lua_pushnumber(L, 0);
	lua_setglobal(L, "TFLAG");

	lua_pushnumber(L, 1);
	lua_setglobal(L, dio_ack);
	
	//set empty neighbor table
	lua_newtable(L);
	lua_setglobal(L, "neighbor_table");
	
	//set empty routing table
	lua_newtable(L);
	lua_setglobal(L, "routing_table");

}

//(Payload, srcip, srcport)
//function called when dio msg received on eport
//actions to perform upon receipt of dio

int disdio_callback(lua_State *L)
{
	lua_pushlightfunction(L, libmsgpack_mp_unpack);
	lua_pushvalue(L, 1);
	lua_call(L, 1, 1);
	int tab_index=lua_gettop(L); //DIO received
	int s_rank, p_rank, //rank of self and incoming parent node
	int nid; //node id of incoming DIO
	lua_pushnil(L);
	while (lua_next(L, -2))
	{
		const char* k= lua_tostring(L, -2);
		if(strcmp(k, "rank")==0)
		{
			p_rank = lua_tonumber(L, -1));
			break;
		}
		if(strcmp(k, "node_id")==0)
		{
			nid= lua_tonumber(L, -1);
		}
		lua_pop(L, 1);
	}
	char *srcip = lua_getstring(L, 2);
	uint16_t srcport = lua_getnumber(L, 3);
	
	
	
	lua_getglobal(L, "DIO")
	int self_dio= lua_gettop(L); 
	lua_pushnil(L);
	while (lua_next(L, -2))
	{
		const char* k= lua_tostring(L, -2);
		if(strcmp(k, "rank")==0)
		{
			s_rank = lua_tonumber(L, -1));
		}
		lua_pop(L, 1);
	}
	
	//check if the node is floating or has a rank greater than new parent
	if(s_rank == -1 || s_rank > (p_rank + 1))
	{
		//set preferred parent as incoming DIO, which is on top of the stack
		lua_pushvalue(L, tab_index);
		lua_setglobal(L, "PrefParent");

		//Calculate rank and update grounded status
		lua_pushstring(L, "rank");
		lua_pushnumber(L, p_rank+1);
		lua_settable(L, self_dio);
		
		lua_pushstring(L, "GF"); 
		lua_pushnumber(L, 1);
		lua_settable(L, self_dio);
		
		lua_setglobal(L, "DIO");
		
		//add to neighbor list
		lua_getglobal(L, "neighbor_table");
		lua_pushnumber(L, nid);
		lua_pushstring(L, srcip);
		lua_settable(L, -3);
		lua_setglobal(L, "neighbor_table");

	
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
		//consistent state, increment C
		lua_getglobal(L, "C");
		lua_pushnumber(L, lua_tonumber(L, -1)+1);
		lua_setglobal(L, "C");
	}
	
	return 0;
}
