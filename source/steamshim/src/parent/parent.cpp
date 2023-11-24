#include <cstddef>
#include <cstdint>

#include "parent_private.h"
#include "../steamshim_private.h"
#include "../os.h"
#include "../steamshim.h"
#include "steam/steam_api.h"
#include "steam/steam_gameserver.h"

int GArgc = 0;
char **GArgv = NULL;

typedef enum ServerType {
    STEAMGAMESERVER,
    STEAMGAMECLIENT,
} ServerType;
static ServerType GServerType;

static ISteamUserStats *GSteamStats = NULL;
static ISteamUtils *GSteamUtils = NULL;
static ISteamUser *GSteamUser = NULL;
static AppId_t GAppID = 0;
static uint64 GUserID = 0;
// static SteamBridge *GSteamBridge = NULL;
static ISteamGameServer *GSteamGameServer = NULL;


static bool processCommand(pipebuff_t cmdbuf, ShimCmd cmd, unsigned int len)
{
  #if 1
    if (false) {}
#define PRINTGOTCMD(x) else if (cmd && cmd == x) printf("Parent got " #x ".\n")
    PRINTGOTCMD(SHIMCMD_BYE);
    PRINTGOTCMD(SHIMCMD_PUMP);
    PRINTGOTCMD(SHIMCMD_REQUESTSTATS);
    PRINTGOTCMD(SHIMCMD_STORESTATS);
    PRINTGOTCMD(SHIMCMD_SETACHIEVEMENT);
    PRINTGOTCMD(SHIMCMD_GETACHIEVEMENT);
    PRINTGOTCMD(SHIMCMD_RESETSTATS);
    PRINTGOTCMD(SHIMCMD_SETSTATI);
    PRINTGOTCMD(SHIMCMD_GETSTATI);
    PRINTGOTCMD(SHIMCMD_SETSTATF);
    PRINTGOTCMD(SHIMCMD_GETSTATF);
    PRINTGOTCMD(SHIMCMD_REQUESTSTEAMID);
    PRINTGOTCMD(SHIMCMD_REQUESTPERSONANAME);
    PRINTGOTCMD(SHIMCMD_SETRICHPRESENCE);
    PRINTGOTCMD(SHIMCMD_REQUESTAUTHSESSIONTICKET);
    PRINTGOTCMD(SHIMCMD_BEGINAUTHSESSION);
#undef PRINTGOTCMD
    else if (cmd != SHIMCMD_PUMP) printf("Parent got unknown shimcmd %d.\n", (int) cmd);
#endif

    pipebuff_t msg;
    switch (cmd)
    {
        case SHIMCMD_PUMP:
            if (GServerType == STEAMGAMESERVER)
                SteamGameServer_RunCallbacks();
            else
                SteamAPI_RunCallbacks();
            break;

        case SHIMCMD_BYE:
            // writeBye(fd);
            return false;

        case SHIMCMD_REQUESTSTEAMID:
            {
                unsigned long long id = SteamUser()->GetSteamID().ConvertToUint64();

                msg.WriteByte(SHIMEVENT_STEAMIDRECIEVED);
                msg.WriteLong(id);
                msg.Transmit();
            }
            break;

        case SHIMCMD_REQUESTPERSONANAME:
            {
                const char* name = SteamFriends()->GetPersonaName();

                msg.WriteByte(SHIMEVENT_PERSONANAMERECIEVED);
                msg.WriteData((void*)name, strlen(name));
                msg.Transmit();
            }
            break;

        case SHIMCMD_SETRICHPRESENCE:
            {
                const char *key = cmdbuf.ReadString();
                const char *val = cmdbuf.ReadString();
                SteamFriends()->SetRichPresence(key,val);
            }
            break;
        case SHIMCMD_REQUESTAUTHSESSIONTICKET:
            {
                char pTicket[AUTH_TICKET_MAXSIZE];
                uint32 pcbTicket;
                GSteamUser->GetAuthSessionTicket(pTicket,AUTH_TICKET_MAXSIZE, &pcbTicket);

                msg.WriteByte(SHIMEVENT_AUTHSESSIONTICKETRECIEVED);
                msg.WriteLong(pcbTicket);
                msg.WriteData(pTicket, AUTH_TICKET_MAXSIZE);
                msg.Transmit();
            }
            break;
        case SHIMCMD_BEGINAUTHSESSION:
            {
                char pTicket[AUTH_TICKET_MAXSIZE];

                uint64 steamID = msg.ReadLong();
                long long cbAuthTicket = msg.ReadLong();
                void* pAuthTicket = msg.ReadData(AUTH_TICKET_MAXSIZE);

                // printf("stewamid: %llu, %llu, -ticket- \n",*(long long*)buf,cbAuthTicket);


                int i = GSteamGameServer->BeginAuthSession(pAuthTicket, cbAuthTicket, steamID);
                if (i != 0){
                    printf("FAILED %i\n",i);
                }
            }
    } // switch

    return 0;
}


static void processCommands()
{
  pipebuff_t buf;

  while (1){

    if (!buf.Recieve())
      continue;

    if (buf.hasmsg){
        volatile unsigned int evlen =buf.ReadInt();

        ShimCmd cmd = (ShimCmd)buf.ReadByte();

        processCommand(buf, cmd, evlen);
    }
  }
} 


static bool initSteamworks(PipeType fd)
{
    if (GServerType == STEAMGAMESERVER) {
        if (!SteamGameServer_Init(0, 27015, 0,eServerModeNoAuthentication,"0.0.0.0"))
            return 0;
        GSteamGameServer = SteamGameServer();
        if (!GSteamGameServer)
            return 0;
        
    }else{
        // this can fail for many reasons:
        //  - you forgot a steam_appid.txt in the current working directory.
        //  - you don't have Steam running
        //  - you don't own the game listed in steam_appid.txt
        if (!SteamAPI_Init())
            return 0;

        GSteamStats = SteamUserStats();
        GSteamUtils = SteamUtils();
        GSteamUser = SteamUser();

        GAppID = GSteamUtils ? GSteamUtils->GetAppID() : 0;
	    GUserID = GSteamUser ? GSteamUser->GetSteamID().ConvertToUint64() : 0;
    }
    //
    // GSteamBridge = new SteamBridge(fd);
    return 1;
} 

static bool setEnvironmentVars(PipeType pipeChildRead, PipeType pipeChildWrite)
{
    char buf[64];
    snprintf(buf, sizeof (buf), "%llu", (unsigned long long) pipeChildRead);
    if (!setEnvVar("STEAMSHIM_READHANDLE", buf))
        return false;

    snprintf(buf, sizeof (buf), "%llu", (unsigned long long) pipeChildWrite);
    if (!setEnvVar("STEAMSHIM_WRITEHANDLE", buf))
        return false;

    return true;
} // setEnvironmentVars


int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    GArgc = argc;
    GArgv = argv;


    PipeType pipeParentRead = NULLPIPE;
    PipeType pipeParentWrite = NULLPIPE;
    PipeType pipeChildRead = NULLPIPE;
    PipeType pipeChildWrite = NULLPIPE;
    ProcessType childPid;

    dbgpipe("Parent starting mainline.\n");

    // temporary hack, make this better
    if (strstr(*GArgv,"warfork_steam"))
        GServerType = STEAMGAMECLIENT;
    else
        GServerType = STEAMGAMESERVER;

    if (!createPipes(&pipeParentRead, &pipeParentWrite, &pipeChildRead, &pipeChildWrite))
        fail("Failed to create application pipes");
    else if (!initSteamworks(pipeParentWrite))
        fail("Failed to initialize Steamworks");
    else if (!setEnvironmentVars(pipeChildRead, pipeChildWrite))
        fail("Failed to set environment variables");
    else if (!launchChild(&childPid))
        fail("Failed to launch application");

    // Close the ends of the pipes that the child will use; we don't need them.
    closePipe(pipeChildRead);
    closePipe(pipeChildWrite);
    pipeChildRead = pipeChildWrite = NULLPIPE;

    dbgpipe("Parent in command processing loop.\n");

    // Now, we block for instructions until the pipe fails (child closed it or
    //  terminated/crashed).
    GPipeRead = pipeParentRead;
    GPipeWrite = pipeParentWrite;
    processCommands();

    dbgpipe("Parent shutting down.\n");

    // Close our ends of the pipes.
    // writeBye(pipeParentWrite);
    closePipe(pipeParentRead);
    closePipe(pipeParentWrite);

    // deinitSteamworks();

    dbgpipe("Parent waiting on child process.\n");

    // Wait for the child to terminate, close the child process handles.
    const int retval = closeProcess(&childPid);

    dbgpipe("Parent exiting mainline (child exit code %d).\n", retval);

    return retval;

}

