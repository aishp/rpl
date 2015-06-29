
/* 
THINGS TO DO IN LUA
1. Create DIS msg using function dio_init and dio_create
2. Create DIS Socket
3. Push global variables onto global stack

LIST OF GLOBAL VARIABLES
1. DIOmsg
2. routing_table
3. dis_sock
4. T_FLAG
*/


#include<libstorm.c>
#include<libstorm.h>
#define RPL_SYMBOLS \ 
	{LSTRKEY("rpl_init"), LFUNCVAL(rpl_init)}, \

static const LUA_REG_TYPE rpl_meta_map[] = 
{
 {LSTRKEY("bcast_dis"), LFUNCVAL(bcast_dis)},
 {LSTRKEY("ucast_dio"), LFUNCVAL(ucast_dio)},
 {LSTRKEY("bcast_dio"), LFUNCVAL(bcast_dio)},
 {LNILKEY, LNILVAL}
};

uint16_t cport = 49152;//socket for receiving DIO, unicast DIO
uint16_t dport = 49153;//socket for broadcasting dio according to trickle timer
uint16_t dio_port = 49154; //port on which other nodes broadcast DIS and listen for DIO

storm_socket_t *sock;

struct dio
{
	//DIO Message Fields
	uint8_t instance_id = 1; //8-bit instance id
	uint16_t rank = 1; //16 bit rank
	int grounded; //1 bit flag indicationg of node if grounded (1) or floating(0)
	int mop; //2 bit flag indicating mode of operation (storing/ non-storing)
	uint8_t dtsn;// 8 bit flag used to maintain downward routes
	uint8_t reserved; // 8 bits reserved
	const char *dodag_id; //128 bit IPV6 address
	uint8_t etx; //estimated hops left, 8 bit
};

struct dio *dio_init(struct dio *d, char *node)
{
	if(strcmp(node, "root")==1)
	{
		(*d).instance_id=1; // first instance 
		(*d).rank = 1; // root has rank 1
		(*d).grounded = 0; 
		(*d).mop = 1; //storing mode	
		(*d).dtsn = 0;
		(*d).reserved = 0;
		(*d).dodag_id =1;
		(*d).etx = 10; // maximum estimated hops left
	}
	
	else if(strcmp(node, "leaf")==1)
	{
		(*d).instance_id=1; // first instance 
		(*d).rank = -1; // consider -1 to be rank of floating nodes
		(*d).grounded = 0; 
		(*d).mop = 1; //storing mode	
		(*d).dtsn = 0;
		(*d).reserved = 0;
		(*d).dodag_id =-1;
		(*d).etx = 10; // maximum estimated hops left
	}
	return d;

	
}

char *dio_create(struct dio d)
{
	char *dio_msg;
	//RPL Instance ID (8)
	//Rank(16)
	//Grounded (1) - int
	//MOP (2) - int
	//DTSN (8)
	//Reserved (8)
	//DODAG ID (128) - char *
	//ETX (8)
	sprintf(dio_msg, "%u\n%u\n%d\n%d\n%u\n%u\n%s\n%u\n", d.instance_id, d.rank, d.grounded, d.mop, d.dtsn, d.reserved, d.dodag_id, d.etx);
	printf("The assembled DIO message is: %s\n", dio_msg);
	return dio_msg;		
}

//(Payload, srcip, srcport)
//function called when dis msg received on cport
int dis_callback(lua_State *L)
{

//actions to perform upon receipt of dis	
	char *payload = lua_getstring(L,1);
	char *srcip = lua_getstring(L, 2);
	uint16_t srcport = lua_getnumber(L, 3);

//global routing table gets pushed to top of the stack
	lua_getglobal(L, "routing_table"); //table is at -1
	lua_pushstring(L, srcip);
	lua_pushstring(L, srcport);
	lua_settable(L, -3); //table is at -3 now
	lua_setglobal(L, "routing_table");

//unicast back DIO
//***CREATE DIO MESSAGE SOMEWHERE ***
	
	lua_getglobal(L, "DIOmsg");
	struct dio *msg = lua_touserdata(L, -1);
	int  rv=0; //return value from libstorm_net_sendto

	do
	{
		lua_pushlightfunction(L, libstorm_net_sendto);
//storm.net.sendto(socket, payload, address, destport) -> number
		lua_getglobal(L, "dis_sock");
//storm_socket_t *dsock = lua_touserdata(L, lua_gettop(L));
		lua_pushstring(L, msg);
		lua_pushstring(L, srcip);
		lua_pushnumber(L, dio_port);
		lua_call(L,4,1);
		rv= lua_checknumber(L, gettop(L));
		
	} while(rv!=1);

	lua_getglobal(L, "TFLAG");
	int tflag = lua_checknumber(L, -1);
	
	if(tflag==0)
	{
		//launch trickle timer
	}
	      
}

//(Payload, srcip, srcport)
//function called when dio msg received on eport
//actions to perform upon receipt of dio

int disdio_callback(lua_State *L)
{
	struct dio *msg = lua_touserdata(L,1);
	char *srcip = lua_getstring(L, 2);
	uint16_t srcport = lua_getnumber(L, 3);
	struct dio *new =  lua_touserdata(L, lua_getglobal(L, "DIO"));

	//check if the node is floating or has a rank greater than new parent
	if(*new.rank == -1 || *new.rank > (*msg.rank + 1))
	{
		//set preferred parent
		lua_setglobal(L, "PrefParent");

		//Calculate rank and update grounded status
		new = msg;
		(*new).rank++; //increase rank
		(*new).etx--; //decrease hop count
		lua_pushlightuserdata(L, new);
		lua_setglobal(L, "DIO");

		//add to neighbor list
		lua_getglobal(L, "routing_table");
		lua_pushstring(L, srcip);
		lua_pushnumber(srcport);
		lua_settable(L, -3);
		lua_setglobal(L, "routing_table");
		
	}
}

//for recieving dis and invoking callback
int create_disrecv_socket(lua_State *L)
{
	//create socket for receiving dis messages
	lua_pushlightfunction(L, libstorm_net_udpsocket);
	lua_pushnumber(cport);
	lua_pushlightfunction(dis_callback);
	lua_call(L,2,1);
	lua_setglobal(L, "dis_sock");
	return 0;
}

//for dio broadcast
int create_dio_bsocket(lua_State *L)
{
	lua_pushlightfunction(L, libstorm_net_udpsocket);
	lua_pushnumber(dport);
	lua_pushlightfunction(dio_callback);
	lua_call(L,2,1);
	lua_setglobal(L, "dio_sock");
	return 0;
}

//for multicasting dis and receiving dio unicast/broadcast
int create_disdio_socket(lua_State *L)
{
	lua_pushlightfunction(L, libstorm_net_udpsocket);
	lua_pushnumber(eport);
	lua_pushlightfunction(recv_dio);
	lua_call(L,2,1);
	lua_setglobal(L, "disdio_sock");
	return 0;
}

int bcast_dio(lua_State *L)
{
	//libstorm_net_sendto(socket, payload, ip, port)
	//multicast address: "ff02::1"
	lua_pushlightfunction(L, libstorm_net_sendto);
	lua_getglobal(L, "dio_sock");
	lua_getglobal(L, "DIOmsg");
	lua_pushstring(L, "ff02::1");
	lua_pushnumber(L, dio_port);
	lua_call(L, 4, 1);
	return 0;
	
}	
		
int bcast_dis(lua_State *L)
{
	//libstorm_net_sendto(socket, payload, ip, port)
	//multicast address: "ff02::1"
	lua_pushlightfunction(L, libstorm_net_sendto);
	lua_getglobal(L, "dio_sock");
	lua_getglobal(L, "DIOmsg");
	lua_pushstring(L, "ff02::1");
	lua_pushnumber(L, bcast_port);
	lua_call(L, 4, 1);
	return 0;
}	

int leaf_func(lua_State *L)
{
	lua_pushlightfunction(L, create_disdio_socket);
	lua_call(L, 0, 0);

	struct dio *dio_msg= malloc(sizeof(struct dio));
	dio_msg = dio_init(dio_msg, "leaf");
	lua_pushlightuserdata(L, dio_msg);
	lua_setglobal(L, "DIO");

	lua_newtable(L);
	lua_setglobal(L, "routing_table");

	//continue broadcasting DIS till node is grounded
	do
	{
		lua_pushlightfunction(L, bcast_dis);
		//**wait for a while**
		
		lua_getglobal(L, "DIO");
		dio_msg= lua_touserdata(L, -1);
		if((*dio_msg).grounded == 1)
		{
			break;
		}
	}while(1);
}

//actions that take place in a root node
int root_func(lua_State *L)
{
	lua_pushlightfunction(L, create_dis_socket);
	lua_call(L, 0, 0);

	lua_pushlightfunction(L, create_dio_bsocket);
	lua_call(L, 0, 0);

	struct dio *msg = malloc(sizeof(struct dio));
	msg = dio_init(msg, "root");
	lua_pushlightuserdata(L, msg);
	lua_setglobal(L, "DIOmsg");

	lua_pushnumber(L, 0);
	lua_setglobal(L, "TFLAG");

	lua_newtable(L);
	lua_setglobal(L, "routing_table");

}
	
	

