//time and stdlib required for random number generator
#include<time.h> 
#include<stdlib.h> 

#define data_port 49160

int t_timeout(lua_State *L)
{
	lua_pushnumber(L, 1);
	lua_setglobal(L, "t_timeout_flag");
}

int i_timeout(lua_State *L)
{

	lua_pushnumber(L, 1);
	lua_setglobal(L, "i_timeout_flag");
}

int trickle_counter_callback(lua_State *L)
{
	struct dio msg = lua_touserdata(L,1);
	char *srcip = lua_getstring(L, 2);
	uint16_t srcport = lua_getnumber(L, 3);

	
	lua_getglobal(L, "C");
	int c= lua_tonumber(L, -1);

	lua_getglobal(L, "DIO"); //DIO of child nodes
	struct dio self_dio = lua_touserdata(L, -1);

	if(msg.rank+1 < self_dio.rank)
	{
		lua_pushnumber(L, 0);
		lua_setglobal(L, "C");
	}

	else
	{
		c++;
		lua_pushnumber(L, c);
		lua_setglobal(L, "C");
	}

	
}

int create_data_socket(lua_State *L)
{
	
	lua_pushlightfunction(L, libstorm_net_udpsocket);
	lua_pushnumber(data_port);
	lua_pushlightfunction(trickle_counter_callback);
	lua_call(L,2,1);
	lua_setglobal(L, "data_sock");
	return 0;
}
int trickle_timer(lua_State *L)
{
	//initialize random number generator
	srand(time(NULL)); 
	
	//minimum interval size in seconds
	int imin = 5; 
	
	//number of doublings of imin	
	int imax = 16; 
	
	//number of consistent messages for no transmission
	int k = 2; 

	//current interval size, random number between imin and imax
 	int i = (rand() % ((imin* (2^imax))-imin)) + 1; 
	
	//counter	
	int c=0; 
	
	// a time within the current interval, random number between (i/2) and i
	int t = (rand() % ((i)-(i/2))) + (i/2);

	//create a timeout flag
	lua_pushnumber(L, 0);
	lua_setglobal(L, "timeout_flag");

	//variable for checking timeout flag
	int tflag;
	int iflag;

	//create socket for listening to data
	create_data_socket(L);

	//start first interval 
	//while timeout_flag=0, keep listening on port
	do
	{
		lua_pushlightfunction(L, libstorm_os_invoke_later);
		lua_pushnumber(L, (t) * SECOND_TICKS);
		lua_pushvalue(L, t_timeout);
		lua_call(L, 2, 0);

		lua_pushlightfunction(L, libstorm_os_invoke_later);
		lua_pushnumber(L, (t) * SECOND_TICKS);
		lua_pushvalue(L, i_timeout);
		lua_call(L, 2, 0);
	
		do
		{
			lua_getglobal(L, "t_timeout_flag");
			tflag= lua_tonumber(L, -1);

		}while (tflag!=0);

		//now interval is over
		lua_getglobal(L, "C");
		c=lua_tonumber(L, -1);
	
		if(c<k)
		{
			//transmit
		
		}

		else
		{
			//reset all values
			lua_pushnumber(L, 0);
			lua_setglobal(L, "C");
			i = imin;
			t = (rand() % ((i)-(i/2))) + (i/2);
			break; 
		}

		do
		{
			lua_getglobal(L, "i_timeout_flag");
			iflag= lua_tonumber(L, -1);

		}while (iflag!=0);

		//i timed out, double i
		if((2*i)<(imin*(2^imax)))
		{
			i*=2;
		}
	
		else
		{
			i = imin * (2^imax);
		}

	}while(1);

}
