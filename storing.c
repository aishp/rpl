//STORING MODE

#define dao_port 49161

struct dao
{
	uint16_t node_id;
	char *dst; //IP address of destination from root (node that initiates DAO)
	char **route; //Starts with parent of DST
	int n; //number of hops
};

int dao_callback(lua_State *L)
{

	struct dao msg = lua_touserdata(L,1);
	char *srcip = lua_getstring(L, 2);
	uint16_t srcport = lua_getnumber(L, 3);
	
	int rv=0;
	
	lua_getglobal(L, "DIO");
	struct dio self_dio = lua_touserdata(L, -1);
	if(self_dio.rank==1) //root
	{
		//add route to routing table
		lua_getglobal(L, "routing_table");
		lua_pushnumber(L, msg.node_id);
		lua_pushlightuserdata(L, route);
		lua_settable(L, -3);
		lua_setglobal(L, "routing_table");
	}
	else
	{
		//add self_ip to route and forward DAO packet to preferred parent
		lua_pushlightfunction(L, libstorm_os_getipaddrstring);
		lua_call(L, 0, 1);
		//as defined in libstorm.c (static char sip[40])
		char self_ip[40] = lua_tostring(L, -1);
		strcpy(msg.route[msg.n], self_ip);
		msg.n++;
		lua_pushlightfunction(L, libstorm_net_sendto);
		lua_getglobal(L, "dao_sock"); //socket
		lua_pushlightuserdata(L, msg); //payload (should it be packed?)
		
		 //Dest Addr (parent ip)
		lua_getglobal(L, "PrefParent");
		struct *pdio= lua_touserdata(L, -1);
		lua_pop(L, 1); //pop DIO from stack
		lua_pushstring(L, pdio->sip); //Parent IP Address for forwarding
		
		lua_pushnumber(L,dao_port); //destination port same as recieving port
		
		while(rv!=1)
		{
			lua_call(L, 4, 1);
			//**WAIT FOR A WHILE
			rv = lua_tonumber(L, -1);
		}
		
	}
}
int create_dao_socket(lua_State *L)
{
	lua_pushlightfunction(L, libstorm_net_udpsocket);
	lua_pushnumber(dao_port);
	lua_pushlightfunction(dao_callback);
	lua_call(L,2,1);
	lua_setglobal(L, "dao_sock");
	return 0;
}

int send_dao(lua_State *L)
{
	//Create and send DAO
	struct dao *d= new struct dio();
	int rv=0;
	
	lua_pushlightfunction(L,libstorm_os_getnodeid);
	lua_call(L, 0, 1);
	d->node_id = lua_tonumber(L, -1);
	
	lua_pushlightfunction(L, libstorm_os_getipaddrstring);
	lua_call(L, 0, 1);
	d->dst= lua_tostring(L, -1); //CHECK IF THIS IS POSSIBLE
	
	d->n=0;
	
	lua_pushlightfunction(L, libstorm_net_sendto);
	lua_getglobal(L, "dao_sock"); //socket
	lua_pushlightuserdata(L, msg); //payload (should it be packed?)
		
	 //Dest Addr (parent ip)
	lua_getglobal(L, "PrefParent");
	struct *pdio= lua_touserdata(L, -1);
	lua_pop(L, 1); //pop DIO from stack
	lua_pushstring(L, pdio->sip); //Parent IP Address for forwarding
		
	lua_pushnumber(L,dao_port); //destination port same as recieving port
		
	while(rv!=1)
	{
		lua_call(L, 4, 1);
		//**WAIT FOR A WHILE
		rv = lua_tonumber(L, -1);
	}
}
