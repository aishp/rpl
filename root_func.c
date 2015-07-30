#define RPL_SYMBOLS \
	{ LSTRKEY("ground_func"), LFUNCVAL(rpl_ground_func)}, \
#define disport 49152

//DIO Message Fields
struct dio
{
	uint8_t instance_id = 1; //8-bit instance id
	uint16_t rank = 1; //16 bit rank
	int grounded; //1 bit flag indicationg of node if grounded (1) or floating(0)
	int mop; //2 bit flag indicating mode of operation (storing/ non-storing)
	uint8_t dtsn;// 8 bit flag used to maintain downward routes
	uint8_t reserved; // 8 bits reserved
	uint16_t *dodag_id; //128 bit IPV6 address
	uint8_t etx; //estimated hops left, 8 bit
	const char *sip; //self ip address
	uint16_t nid; //self node id
};

//actions to perform upon receipt of dis	
int dis_callback(lua_State *L)
{
	struct dis *payload = lua_touserdata(L,1);
	char *srcip = lua_getstring(L, 2);
	uint32_t srcport = lua_getnumber(L, 3);
	
	//trickle timer parameters;
	int imin = 5; 
	int imax = 16; 
	int k = 1; 
	int tflag;
	
	
	int  rv=0; //return value from libstorm_net_sendto
	
	lua_getglobal(L, "neighbor_table"); 
	lua_pushnumber(L, payload->dodag_id);// push node id
	lua_pushstring(L, srcip);
	lua_settable(L, -3);
	lua_setglobal(L, "neighbor_table");

//unicast back DIO
	
	lua_getglobal(L, "DIOmsg");
  struct dio *msg = lua_touserdata(L, -1);
  lua_pop(L);
  
	do
	{
		lua_pushlightfunction(L, libstorm_net_sendto);
    //storm.net.sendto(socket, payload, address, destport) -> number
		lua_getglobal(L, "dis_sock");
		lua_getglobal(L, "DIOmsg");
		lua_pushstring(L, srcip);
		lua_pushnumber(L, dio_port);
		lua_call(L,4,1);
		rv= lua_checknumber(L, gettop(L));
		
	} while(rv!=1);

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

int create_dis_socket(lua_State *L)
{
	//create socket for receiving dis messages
	lua_pushlightfunction(L, libstorm_net_udpsocket);
	lua_pushnumber(disport);
	lua_pushlightfunction(dis_callback);
	lua_call(L,2,1);
	lua_setglobal(L, "dis_sock");
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

	//Create self DIO msg, initialize as root
	struct dio *msg = malloc(sizeof(struct dio));
	msg = dio_init(msg, "root");
	
	lua_pushlightfunction(L, libstorm_os_getnodeid);
	lua_call(L, 0, 1);
	msg->dodag_id=lua_tonumber(L, -1);
	lua_pop(L); //remove node id from stack
	
	lua_pushlightuserdata(L, msg);
	lua_setglobal(L, "DIOmsg");

	//create and set global DIS msg
	struct dis *dis_msg = malloc(sizeof(struct dis));
	dis_msg->rank=1;
	dis_msg->dodag_id= msg->dodag_id;
	lua_pushlightuserdata(L, dis_msg);
	lua_setglobal(L, "DISmsg");
	
	//set TFLAg to 0 to show that trickle timer is not currently running
	lua_pushnumber(L, 0);
	lua_setglobal(L, "TFLAG");

	//set empty neighbor table
	lua_newtable(L);
	lua_setglobal(L, "neighbor_table");
	
	//set empty routing table
	lua_newtable(L);
	lua_setglobal(L, "routing_table");

}
