/*
Copyright (C) 1994-1995 Apogee Software, Ltd.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#ifdef PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#define NOUSER
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#ifdef DOS
#include <conio.h>
#include <dos.h>
#include <process.h>
#include <bios.h>
#endif

#include "rt_def.h"
#include "_rt_com.h"
#include "rt_com.h"
#include "rt_util.h"
#include "rt_in.h"
#include "rt_crc.h"
#include "rt_playr.h"
#include "isr.h"
#include "rt_msg.h"
#include "rottnet.h"
#include "rt_main.h"
#include "rt_net.h"
#include "rt_draw.h"
//#include "rt_ser.h"
//MED
#include "memcheck.h"

// GLOBAL VARIABLES

// Same as in real mode
rottcom_t   *	rottcom;
int badpacket;
int consoleplayer;
byte ROTTpacket[MAXCOMBUFFERSIZE];
int controlsynctime;

// LOCAL VARIABLES
#ifdef DOS
static union  REGS   comregs;
#endif

static int    ComStarted=false;
static int    transittimes[MAXPLAYERS];

void SyncTime( int client );
void SetTransitTime( int client, int time );

#ifdef PLATFORM_UNIX

static int sock = -1;

static void ReadUDPPacket()
{
	rottcom->remotenode = -1;
}

static void WriteUDPPacket()
{
}

#endif

#ifdef PLATFORM_WIN32

#define ROTT_DEFAULT_PORT 34858
#define ROTT_NET_MAGIC    0x504f4352UL /* "RCOP" */
#define ROTT_HELLO        1
#define ROTT_WELCOME      2

typedef struct
   {
   unsigned long magic;
   unsigned char type;
   unsigned char player;
   unsigned char players;
   unsigned char reserved;
   } rott_udp_handshake_t;

static SOCKET udp_socket = INVALID_SOCKET;
static struct sockaddr_in udp_peer;
static bool udp_is_server = false;
static bool udp_loopback_ready = false;
static short udp_loopback_length = 0;
static byte udp_loopback_data[MAXCOMBUFFERSIZE];

static int ROTT_Port(void)
   {
   int arg = CheckParm("port");
   int port = ROTT_DEFAULT_PORT;

   if (arg && (arg < _argc - 1))
      port = atoi(_argv[arg + 1]);
   if ((port < 1) || (port > 65535))
      Error("Invalid network port %d.\n",port);
   return port;
   }

static void ROTT_SetNonBlocking(void)
   {
   u_long enabled = 1;
   if (ioctlsocket(udp_socket,FIONBIO,&enabled) == SOCKET_ERROR)
      Error("Could not make the network socket non-blocking (%d).\n",
         WSAGetLastError());
   }

static void ROTT_InitUDP(void)
   {
   WSADATA winsock;
   struct sockaddr_in local;
   rott_udp_handshake_t message;
   int port = ROTT_Port();
   int addrlen;
   int received;
   int netarg;

   if (WSAStartup(MAKEWORD(2,2),&winsock) != 0)
      Error("Could not initialize Winsock.\n");

   udp_socket = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
   if (udp_socket == INVALID_SOCKET)
      Error("Could not create UDP socket (%d).\n",WSAGetLastError());

   memset(&local,0,sizeof(local));
   local.sin_family = AF_INET;
   local.sin_addr.s_addr = htonl(INADDR_ANY);
   local.sin_port = htons(CheckParm("server") ? port : 0);
   if (bind(udp_socket,(struct sockaddr *)&local,sizeof(local)) == SOCKET_ERROR)
      Error("Could not bind UDP port %d (%d).\n",port,WSAGetLastError());

   memset(&message,0,sizeof(message));
   message.magic = ROTT_NET_MAGIC;

   if (CheckParm("server"))
      {
      struct sockaddr_in from;

      udp_is_server = true;
      printf("Waiting for a co-op player on UDP port %d...\n",port);
      fflush(stdout);
      do
         {
         addrlen = sizeof(from);
         received = recvfrom(udp_socket,(char *)&message,sizeof(message),0,
            (struct sockaddr *)&from,&addrlen);
         }
      while ((received != sizeof(message)) ||
         (message.magic != ROTT_NET_MAGIC) || (message.type != ROTT_HELLO));

      udp_peer = from;
      message.type = ROTT_WELCOME;
      message.player = 2; /* raw ROTT node; fixed to player 1 by rt_net.c */
      message.players = 2;
      sendto(udp_socket,(const char *)&message,sizeof(message),0,
         (struct sockaddr *)&udp_peer,sizeof(udp_peer));
      printf("Co-op player connected.\n");
      fflush(stdout);
      }
   else
      {
      struct addrinfo hints;
      struct addrinfo *addresses = NULL;
      char porttext[16];
      int attempts = 0;
      DWORD timeout = 500;

      netarg = CheckParm("net");
      if (!netarg || (netarg >= _argc - 1))
         Error("Client usage: -net <host address> [-port <port>]\n");

      memset(&hints,0,sizeof(hints));
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_DGRAM;
      sprintf(porttext,"%d",port);
      if (getaddrinfo(_argv[netarg + 1],porttext,&hints,&addresses) != 0)
         Error("Could not resolve co-op host '%s'.\n",_argv[netarg + 1]);
      memcpy(&udp_peer,addresses->ai_addr,sizeof(udp_peer));
      freeaddrinfo(addresses);

      setsockopt(udp_socket,SOL_SOCKET,SO_RCVTIMEO,(const char *)&timeout,
         sizeof(timeout));
      printf("Connecting to co-op host %s:%d...\n",_argv[netarg + 1],port);
      fflush(stdout);
      message.type = ROTT_HELLO;
      do
         {
         sendto(udp_socket,(const char *)&message,sizeof(message),0,
            (struct sockaddr *)&udp_peer,sizeof(udp_peer));
         addrlen = sizeof(udp_peer);
         received = recvfrom(udp_socket,(char *)&message,sizeof(message),0,
            (struct sockaddr *)&udp_peer,&addrlen);
         attempts++;
         if (attempts >= 120)
            Error("Timed out connecting to the co-op host.\n");
         }
      while ((received != sizeof(message)) ||
         (message.magic != ROTT_NET_MAGIC) || (message.type != ROTT_WELCOME));

      rottcom->consoleplayer = message.player;
      rottcom->numplayers = message.players;
      printf("Connected as player %d.\n",message.player - 1);
      fflush(stdout);
      }

   ROTT_SetNonBlocking();
   }

static void ReadUDPPacket(void)
   {
   struct sockaddr_in from;
   int fromlen = sizeof(from);
   int received;

   rottcom->remotenode = -1;
   if (udp_loopback_ready)
      {
      memcpy(rottcom->data,udp_loopback_data,udp_loopback_length);
      rottcom->datalength = udp_loopback_length;
      rottcom->remotenode = 1;
      udp_loopback_ready = false;
      return;
      }

   received = recvfrom(udp_socket,(char *)rottcom->data,MAXCOMBUFFERSIZE,0,
      (struct sockaddr *)&from,&fromlen);
   if (received == SOCKET_ERROR)
      {
      int error = WSAGetLastError();
      if ((error == WSAEWOULDBLOCK) || (error == WSAECONNRESET))
         return;
      Error("UDP receive failed (%d).\n",error);
      }

   rottcom->datalength = (short)received;
   rottcom->remotenode = udp_is_server ? 2 : 1;
   }

static void WriteUDPPacket(void)
   {
   if (udp_is_server && (rottcom->remotenode == 1))
      {
      memcpy(udp_loopback_data,rottcom->data,rottcom->datalength);
      udp_loopback_length = rottcom->datalength;
      udp_loopback_ready = true;
      return;
      }

   if (sendto(udp_socket,(const char *)rottcom->data,rottcom->datalength,0,
      (struct sockaddr *)&udp_peer,sizeof(udp_peer)) == SOCKET_ERROR)
      Error("UDP send failed (%d).\n",WSAGetLastError());
   }

#endif

/*
===============
=
= InitROTTNET
=
===============
*/

void InitROTTNET (void)
{
	int netarg;
	long netaddress;

	if (ComStarted==true)
		return;
	ComStarted=true;

#ifdef DOS
        netarg=CheckParm ("net");
	netarg++;

	netaddress=atol(_argv[netarg]);
	rottcom=(rottcom_t *)netaddress;
#elif defined(PLATFORM_UNIX) || defined(PLATFORM_WIN32)
	/*
	server-specific options:
	-net: enables netplay
	-server: run as rott server (default port 34858)
	-port: select a non-default port for server
	-host: select a non-default ip address to bind to
	-standalone: run as standalone server
	-remoteridicule: enable remote ridicule
	-players: number of players to expect

	client-specific options:
	-master: request to have control
	-net: specifies the host to connect to
	-port: select a non-default port to connect to
	*/
	
        rottcom = (rottcom_t *) malloc (sizeof(rottcom_t));
        memset(rottcom, 0, sizeof(rottcom_t));
        
        rottcom->ticstep = 1;
        rottcom->gametype = 1;
        rottcom->remotenode = -1;
        
        if (CheckParm("server")) {
        	if (CheckParm("standalone")) {
        		rottcom->consoleplayer = 0;
        	} else {
        		rottcom->consoleplayer = 1;
        	}
        	
        	if (CheckParm("remoteridicule")) {
        		rottcom->remoteridicule = 1;
        	} else {
        		rottcom->remoteridicule = 0;
        	}
        	
        	netarg = CheckParm("players");
        	if (netarg && netarg < _argc-1) {
        		rottcom->numplayers = atoi(_argv[netarg+1]);
        	} else {
        		rottcom->numplayers = 2;
        	}
        	
        	rottcom->client = 0;
        } else {
        	rottcom->client = 1;
        	
        	/* consoleplayer will be initialized after connecting */
        	/* numplayers will be initialized after connecting */
        	/* remoteridicule will be initialized after connecting */
        }

#ifdef PLATFORM_WIN32
        ROTT_InitUDP();
#endif
        
        /* client-server negotiation protocol, as inspired by ipxsetup.c */
        /*
          Best case:
          client sends a HereIAm packet.
          server replies with a YouAre packet, and broadcasts an Info packet
            to the rest, indicating that the new player has joined.
          client replies with an IAm packet
          until all players have joined, the server broadcasts an Info packet
            to the rest, indicating that another player is needed.
          once server has enough players, it broadcasts an AllDone packet.
          
          In detail:
          Client state: HereIAm (initial connection)
          Client sends HereIAm packet, waits for YouAre from server.
          At timeout, Client resends HereIam
          When client receives YouAre, client switches to IAm state
          
          Client state: IAm
          Client sends IAm packet, waits for IAmAck from server.
          At timeout, Client resends IAm
          When client receives IAmAck, client switches to WaitForDone state.
          
          Client state: WaitForDone
          Client waits for AllDone packet.
          When client receives AllDone, it sends an AllDoneAck.
         */
        
#endif

   remoteridicule = false;
   remoteridicule = rottcom->remoteridicule;
   if (rottcom->ticstep != 1)
      remoteridicule = false;
   if (remoteridicule == true)
      {
      if (!quiet)
         printf("ROTTNET: LIVE Remote Ridicule Enabled\n");
      }

   if (!quiet)
      {
#ifdef DOS
      printf("ROTTNET: Communicating on vector %ld\n",(long int)rottcom->intnum);
#endif
      printf("ROTTNET: consoleplayer=%ld\n",(long int)rottcom->consoleplayer);
      }
}

/*
================
=
= ReadPacket
=
================
*/

bool ReadPacket (void)
{
   word   crc;
   word   sentcrc;

   // Set command (Get Packet)
	rottcom->command=CMD_GET;

   badpacket = 0;

   // Check to see if a packet is ready

#ifdef DOS
	int386(rottcom->intnum,&comregs,&comregs);
#elif defined(PLATFORM_UNIX) || defined(PLATFORM_WIN32)
	ReadUDPPacket();
#endif

   // Is it ready?

   if (rottcom->remotenode!=-1)
      {
      // calculate crc on packet
      crc=CalculateCRC ((byte *)&rottcom->data[0], rottcom->datalength-sizeof(word));

      // get crc inside packet
      sentcrc=*((word *)(&rottcom->data[rottcom->datalength-sizeof(word)]));

      // are the crcs the same?
      if (crc!=sentcrc)
         {
         badpacket=1;
         SoftError("BADPKT at %d\n",GetTicCount());
         }
      if (networkgame==false)
         {
         rottcom->remotenode=server;
         }
      else
         {
         if ((IsServer==true) && (rottcom->remotenode>0))
            rottcom->remotenode--;
         }
      memcpy(&ROTTpacket[0], &rottcom->data[0], rottcom->datalength);

//      SoftError( "ReadPacket: time=%ld size=%ld src=%ld type=%d\n",GetTicCount(), rottcom->datalength,rottcom->remotenode,rottcom->data[0]);

#if 0
      rottcom->command=CMD_OUTQUEBUFFERSIZE;
      int386(rottcom->intnum,&comregs,&comregs);
      SoftError( "outque size=%ld\n",*((short *)&(rottcom->data[0])));
      rottcom->command=CMD_INQUEBUFFERSIZE;
      int386(rottcom->intnum,&comregs,&comregs);
      SoftError( "inque size=%ld\n",*((short *)&(rottcom->data[0])));
#endif
      return true;
      }
   else // Not ready yet....
      return false;
}


/*
=============
=
= WritePacket
=
=============
*/

void WritePacket (void * buffer, int len, int destination)
{
   word      crc;

   // set send command
	rottcom->command=CMD_SEND;

   // set destination
   rottcom->remotenode=destination;

   if (len>(int)(MAXCOMBUFFERSIZE-sizeof(word)))
      {
      Error("WritePacket: Overflowed buffer\n");
      }

   // copy local buffer into realmode buffer
   memcpy((byte *)&(rottcom->data[0]),(byte *)buffer,len);

   // calculate CRC
   crc=CalculateCRC (buffer, len);

   // put CRC into realmode buffer packet
   *((word *)&rottcom->data[len])=crc;

   // set size of realmode packet including crc
   rottcom->datalength=len+sizeof(word);

   if (*((byte *)buffer)==0)
       Error("Packet type = 0\n");

   if (networkgame==true)
      {
      if (IsServer==true)
         rottcom->remotenode++; // server fix-up
      }

//   SoftError( "WritePacket: time=%ld size=%ld src=%ld type=%d\n",GetTicCount(),rottcom->datalength,rottcom->remotenode,rottcom->data[0]);
   // Send It !
#ifdef DOS
	int386(rottcom->intnum,&comregs,&comregs);
#elif defined(PLATFORM_UNIX) || defined(PLATFORM_WIN32)
	WriteUDPPacket();
#endif

#if 0
   rottcom->command=CMD_OUTQUEBUFFERSIZE;
   int386(rottcom->intnum,&comregs,&comregs);
   SoftError( "outque size=%ld\n",*((short *)&(rottcom->data[0])));
   rottcom->command=CMD_INQUEBUFFERSIZE;
   int386(rottcom->intnum,&comregs,&comregs);
   SoftError( "inque size=%ld\n",*((short *)&(rottcom->data[0])));
#endif
}




/*
=============
=
= ValidSyncPacket
=
=============
*/
bool ValidSyncPacket ( synctype * sync )
{
   if (ReadPacket() && (badpacket==0))
      {
      if (((syncpackettype *)&(ROTTpacket[0]))->type==COM_SYNC)
         {
         memcpy(&(sync->pkt),&(ROTTpacket[0]),sizeof(sync->pkt));
         return true;
         }
      }
   return false;
}

/*
=============
=
= SendSyncPacket
=
=============
*/
void SendSyncPacket ( synctype * sync, int dest)
{
   sync->pkt.type=COM_SYNC;
   sync->sendtime=GetTicCount();
   WritePacket( &(sync->pkt.type) , sizeof(syncpackettype) , dest );
}


/*
=============
=
= SlavePhaseHandler
=
=============
*/

bool SlavePhaseHandler( synctype * sync )
{
   bool done;

   done=false;

   switch (sync->pkt.phase)
      {
      case SYNC_PHASE1:
         break;
      case SYNC_PHASE2:
         ISR_SetTime(sync->pkt.clocktime);
         break;
      case SYNC_PHASE3:
         sync->pkt.clocktime=GetTicCount();
         break;
      case SYNC_PHASE4:
         ISR_SetTime(GetTicCount()-sync->pkt.delta);
         sync->pkt.clocktime=GetTicCount();
         break;
      case SYNC_PHASE5:
         ISR_SetTime(GetTicCount()-sync->pkt.delta);
         sync->sendtime=sync->pkt.clocktime;
         done=true;
         break;
      }
   return done;
}



/*
=============
=
= MasterPhaseHandler
=
=============
*/

bool MasterPhaseHandler( synctype * sync )
{
   bool done;

   done=false;

   switch (sync->pkt.phase)
      {
      case SYNC_PHASE1:
         sync->pkt.phase=SYNC_PHASE2;
         sync->pkt.clocktime=GetTicCount()+(sync->deltatime>>1);
         break;
      case SYNC_PHASE2:
         sync->pkt.phase=SYNC_PHASE3;
         break;
      case SYNC_PHASE3:
         sync->pkt.delta=sync->pkt.clocktime-GetTicCount()+(sync->deltatime>>1);
         sync->pkt.phase=SYNC_PHASE4;
         break;
      case SYNC_PHASE4:
         sync->pkt.phase=SYNC_PHASE5;
         sync->pkt.delta=sync->pkt.clocktime-GetTicCount()+(sync->deltatime>>1);
         sync->sendtime=GetTicCount()+SYNCTIME;
         sync->pkt.clocktime=sync->sendtime;
         done=true;
         break;
      }
   return done;
}


/*
=============
=
= ComSetTime
=
=============
*/

void ComSetTime ( void )
{
   int i;
   syncpackettype * syncpacket;
   bool done=false;

   syncpacket=(syncpackettype *)SafeMalloc(sizeof(syncpackettype));


   // Sync clocks

   if (networkgame==true)
      {
      if (IsServer==true)
         {
         for (i=0;i<numplayers;i++)
            {
            if (PlayerInGame(i)==false)
               continue;
            if (standalone==true)
               SyncTime(i);
            else if (i!=consoleplayer)
               SyncTime(i);
            if (standalone==true)
               printf("ComSetTime: player#%ld\n",(long int)i);
            }
         }
      else
         {
         SyncTime(0);
         }
      }
   else // Modem 2-player game
      {
      if (consoleplayer==0)
         SyncTime(server);
      else
         SyncTime(server);
      }

   if ( ( (networkgame==true) && (IsServer==true) ) ||
        ( (networkgame==false) && (consoleplayer==0) )
      ) // Master/Server
      {
      int nump;
      int time;

      syncpacket->type=COM_START;
      syncpacket->clocktime=GetTicCount();
      controlsynctime=syncpacket->clocktime;
      if (networkgame==true)
         nump=numplayers;
      else
         nump=1;

      time = GetTicCount();

      for (i=0;i<nump;i++)
         {
         WritePacket( &(syncpacket->type) , sizeof(syncpackettype) , i );
         }

      while (GetTicCount()<time+(VBLCOUNTER/4)) ;

      for (i=0;i<nump;i++)
         {
         WritePacket( &(syncpacket->type) , sizeof(syncpackettype) , i );
         }

      if (standalone==true)
         printf("ComSetTime: Start packets sent\n");
      }
   else // Slave/Client
      {
      while (done==false)
         {
         AbortCheck("ComSetTime aborted as client");

         if (ReadPacket() && (badpacket==0))
            {
            memcpy(syncpacket,&(ROTTpacket[0]),sizeof(syncpackettype));
            if (syncpacket->type==COM_START)
               {
               controlsynctime=syncpacket->clocktime;
               done=true;
               }
            }
         }
      }
   if (standalone==false)
      {
      AddMessage("All players synched.",MSG_SYSTEM);
      ThreeDRefresh();
      }
   SafeFree(syncpacket);

//
// flush out any extras
//
   while (GetTicCount()<controlsynctime+VBLCOUNTER)
      {
      ReadPacket ();
      }
}

/*
=============
=
= InitialMasterSync
=
=============
*/
void InitialMasterSync ( synctype * sync, int client )
{
   bool done=false;
   int i;

   if (networkgame==true)
      {
      for (i=0;i<numplayers;i++)
         {
         if (i<=client)
            continue;
         sync->pkt.type=COM_SYNC;
         sync->pkt.phase=SYNC_MEMO;
         sync->pkt.clocktime=client;
         SendSyncPacket(sync,i);
         }
      }

   // Initialize send time so as soon as we enter the loop, we send

   sync->sendtime=GetTicCount()-SYNCTIME;

   while (done==false)
      {
      sync->pkt.phase=SYNC_PHASE0;

      AbortCheck("Initial sync aborted as master");
	   if ((sync->sendtime+SYNCTIME) <= GetTicCount())
         SendSyncPacket(sync,client);
      if (ValidSyncPacket(sync)==true)
         {
         if (sync->pkt.phase==SYNC_PHASE0)
            {
            int time=GetTicCount();

            while (time+SYNCTIME>GetTicCount())
               {
               ReadPacket();
               }
            time=GetTicCount();
            while (time+SYNCTIME>GetTicCount()) {}
            done=true;
            }
         }
      }
}

/*
=============
=
= InitialSlaveSync
=
=============
*/
void InitialSlaveSync ( synctype * sync )
{
   bool done=false;

   while (done==false)
      {
      AbortCheck("Initial sync aborted as slave");
      if (ValidSyncPacket(sync)==true)
         {
         if (sync->pkt.phase==SYNC_MEMO)
            {
            char str[50]="Server is synchronizing player ";
            char str2[10];

            strcat(str,itoa(sync->pkt.clocktime+1,str2,10));
            AddMessage(str,MSG_SYSTEM);
            ThreeDRefresh();
            }
         if (sync->pkt.phase==SYNC_PHASE0)
            {
            int time=GetTicCount();

            SendSyncPacket(sync,server);
            while (time+SYNCTIME>GetTicCount())
               {
               ReadPacket();
               }
            done=true;
            }
         }
      }
   AddMessage("Server is synchronizing your system",MSG_SYSTEM);
   ThreeDRefresh();
}


/*
=============
=
= SyncTime
=
=============
*/

void SyncTime( int client )
{
   int dtime[NUMSYNCPHASES];
   bool done;
   int i;
   synctype * sync;

   sync=(synctype *)SafeMalloc(sizeof(synctype));

   if ( ((networkgame==true) && (IsServer==true)) ||
         ((networkgame==false) && (consoleplayer==0)) )
      {
      // Master

      InitialMasterSync ( sync, client );

      done=false;

      // Initial setup for Master
      // Initialize send time so as soon as we enter the loop, we send

      sync->pkt.phase=SYNC_PHASE1;
      sync->sendtime=GetTicCount()-SYNCTIME;

      while (done==false)
         {
         // Master

         AbortCheck("SyncTime aborted as master");

		   if ((sync->sendtime+SYNCTIME) <= GetTicCount())
            SendSyncPacket(sync,client);

         while (ValidSyncPacket(sync)==true)
            {

            // find average delta

            sync->deltatime=0;

            // calculate last delta

            dtime[sync->pkt.phase]=GetTicCount()-sync->sendtime;

            for (i=0;i<=sync->pkt.phase;i++)
               sync->deltatime+=dtime[i];
            if (i!=0)
               sync->deltatime/=i;
            else
               Error("SyncTime: this should not happen\n");

            done = MasterPhaseHandler( sync );

            SendSyncPacket(sync,client);

            }
         }
      }
   else
      {
      // Slave

      InitialSlaveSync ( sync );

      done=false;

      while (done==false)
         {
         // Slave

         AbortCheck("SyncTime aborted as slave");

         while (ValidSyncPacket(sync)==true)
            {
            done = SlavePhaseHandler( sync );

            if (done==false)
               SendSyncPacket(sync,server);

            }
         }
      }

   while (sync->sendtime > GetTicCount())
      {
      while (ReadPacket()) {}
      }
   while ((sync->sendtime+SYNCTIME) > GetTicCount())
      {
      }

   if ( ((networkgame==true) && (IsServer==true)) ||
         ((networkgame==false) && (consoleplayer==0)) )
      SetTransitTime( client, (sync->deltatime>>1));

   SafeFree(sync);
}

/*
=============
=
= SetTransitTime
=
=============
*/

void SetTransitTime( int client, int time )
{
   transittimes[client]=time;
}

/*
=============
=
= GetTransitTime
=
=============
*/

int GetTransitTime( int client )
{
   return transittimes[client];
}

