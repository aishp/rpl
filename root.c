
/* 
THINGS TO DO IN LUA
1. Create DIS msg using function dio_init and dio_create
2. Create DIS Socket
3. Push global variables onto global stack

LIST OF GLOBAL VARIABLES
1. DIOmsg
2.neighbor_table
3. dis_sock
4. T_FLAG
*/

//time and stdlib required for random number generator
#include<time.h> 
#include<stdlib.h>

//headers for firestorm functions
#include<libstorm.c>
#include<libstorm.h>

//symbol table
#define RPL_SYMBOLS \ 
	{LSTRKEY("float_func"), LFUNCVAL(rpl_float_func)}, \
	{LSTRKEY("ground_func"), LFUNCVAL(rpl_ground_func)}, \

uint16_t cport = 49152;//socket for receiving DIO, unicast DIO
uint16_t dport = 49153;//socket for broadcasting dio according to trickle timer
uint16_t eport = 49154; //port on which other nodes broadcast DIS and listen for DIO

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

//(Payload, srcip, srcport)
//function called when dis msg received on cport
int dis_callback(lua_State *L)
{

//actions to perform upon receipt of dis	
	char *payload = lua_getstring(L,1);
	char *srcip = lua_getstring(L, 2);
	uint16_t srcport = lua_getnumber(L, 3);
	
	//trickle timer parameters;
	int imin = 5; 
	int imax = 16; 
	int k = 1; 
	int tflag;
	
	struct dio *msg;
	int  rv=0; //return value from libstorm_net_sendto
	
//global neighbor table gets pushed to top of the stack
	lua_getglobal(L, neighbor_table"); //table is at -1
	lua_pushstring(L, srcip);
	lua_pushstring(L, srcport);
	lua_settable(L, -3); //table is at -3 now
	lua_setglobal(L, neighbor_table");

//unicast back DIO
//***CREATE DIO MESSAGE SOMEWHERE ***
	
	lua_getglobal(L, "DIOmsg");
	msg = lua_touserdata(L, -1);

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

//(Payload, srcip, srcport)
//function called when dio msg received on eport
//actions to perform upon receipt of dio

int disdio_callback(lua_State *L)
{
	struct dio *msg = lua_touserdata(L,1);
	char *srcip = lua_getstring(L, 2);
	uint16_t srcport = lua_getnumber(L, 3);
	
	lua_getglobal(L, "DIO")
	struct dio *new =  lua_touserdata(L, -1); //self dio

	lua_getglobal(L, "C");
	int c= lua_tonumber(L, -1);

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
		lua_getglobal(L, neighbor_table");
		lua_pushstring(L, srcip);
		lua_pushnumber(srcport);
		lua_settable(L, -3);
		lua_setglobal(L, neighbor_table");

		if((*new).rank > ((*msg).rank+1))
		{
			//inconsistent state reached, reset everything and start trickle timer again
			
			//**STOP THE OLD INSTANCE OF TRICKLE TIMER**
			lua_getglobal(L, "TRICKLE_INSTANCE");
			int inst = lua_tonumber(L, -1);
			inst++;
			lua_pushnumber(L, inst);
			lua_setglobal(L, "TRICKLE_INSTANCE");
			
			trickle_timer();
		}
		
	}

	else
	{
		//consistent state, increment C
		c++;
		lua_pushnumber(L, c);
		lua_setglobal(L, "C");
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

int float_func(lua_State *L)
{
	lua_pushlightfunction(L, create_disdio_socket);
	lua_call(L, 0, 0);

	struct dio *dio_msg= malloc(sizeof(struct dio));
	dio_msg = dio_init(dio_msg, "leaf");
	lua_pushlightuserdata(L, dio_msg);
	lua_setglobal(L, "DIO");

	lua_newtable(L);
	lua_setglobal(L, neighbor_table");

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
int ground_func(lua_State *L)
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
	lua_setglobal(L, neighbor_table");

}
	
	

