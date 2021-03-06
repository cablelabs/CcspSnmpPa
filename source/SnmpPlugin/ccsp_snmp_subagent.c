/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2015 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

/**********************************************************************
   Copyright [2014] [Cisco Systems, Inc.]
 
   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at
 
       http://www.apache.org/licenses/LICENSE-2.0
 
   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
**********************************************************************/

#include <signal.h>
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>
#include <unistd.h>
#include "CcspSnmpPlugin.h"

#ifdef USE_PCD_API_EXCEPTION_HANDLING
#include "pcdapi.h"
#endif

#define AGNT_NAME           "ccsp-snmp-subagent"
#define DEF_MASTER_ADDR     "tcp:127.0.0.1:705"

static int keep_running;
static char *xagent_addr = DEF_MASTER_ADDR;
static char *debug_pat = NULL;

static void
stop_server(int signo) 
{
    keep_running = 0;
}

static void usage(void)
{
    fprintf(stderr, "./snmp_subagent [-x XAgentAddress] [-D DebugString] [-h]\n");
}

void parse_arg(int argc, char *argv[])
{
    int opt;
    char *pXgentAddr = NULL;

    while ((opt = getopt(argc, argv, "x:D:h")) != -1) {

        switch (opt) {
        case 'h':
            usage();
            exit(0);

        case 'x':
            if(pXgentAddr) /*RDKB-6910, CID-33279, free unused resources*/
            {
                free(pXgentAddr);
            }
            pXgentAddr  = strdup(optarg);
            xagent_addr = pXgentAddr;
            break;

        case 'D':
            if(debug_pat) /*RDKB-6910, CID-33076, free unused resources*/
            {
                free(debug_pat);
            }
            debug_pat = strdup(optarg);
            break;

        case '?':
            fprintf(stderr, "unknow option `-%c'\n", optopt);
            exit(1);
        }
    }

    if (!xagent_addr)
        xagent_addr = DEF_MASTER_ADDR;

    return;
}

int
main(int argc, char *argv[])
{
    char                            cmd[1024]          = {0};
    FILE                           *fd                 = NULL;
	
    parse_arg(argc, argv);

    snmp_enable_stderrlog();

    /* make us a agentx client. */
    netsnmp_ds_set_string(NETSNMP_DS_APPLICATION_ID, 
            NETSNMP_DS_AGENT_X_SOCKET, xagent_addr);

    netsnmp_ds_set_boolean(NETSNMP_DS_APPLICATION_ID, 
            NETSNMP_DS_AGENT_ROLE, 1);

    if (debug_pat) {
        debug_register_tokens(debug_pat);
        snmp_set_do_debugging(1);
    }

    /* cannot deamonize, COSA_Init() will fail
    if (netsnmp_daemonize(1, 1) != 0) {
        snmp_log(LOG_ERR, "fail to deamonize !!");
        exit(1);
    }
    */

    SOCK_STARTUP;

    init_agent(AGNT_NAME);

    /* initialize mib code here */
    init_ccsp_snmp_plugin();

    /* AGNT_NAME.conf may used */
    init_snmp(AGNT_NAME);
#ifdef USE_PCD_API_EXCEPTION_HANDLING
    printf("Registering PCD exception handler for snmp subagent\n");
    PCD_api_register_exception_handlers( argv[0], NULL );
#endif
    keep_running = 1;
    signal(SIGTERM, stop_server);
    signal(SIGINT, stop_server);
	
    /*This is used for systemd */
    fd = fopen("/var/tmp/snmp_subagent.pid", "w+");
    if ( !fd )
    {
        printf("Create /var/tmp/snmp_subagent.pid error. \n");
        return 1;
    }
    else
    {
        sprintf(cmd, "%d", getpid());
        fputs(cmd, fd);
        fclose(fd);
    }

    snmp_log(LOG_INFO,"%s is up and running.\n", AGNT_NAME);
    system("sysevent set snmp_subagent-status started");
    if (access("/tmp/snmp_subagent_initialized", F_OK) != 0)
    {
       system("print_uptime \"boot_to_snmp_subagent_uptime\"");
    }
    system("touch /tmp/snmp_subagent_initialized");

    /* main loop */
    while(keep_running) {
        agent_check_and_process(1);
    }

    /* cleanup mib code here */
    remove_ccsp_snmp_plugin();

    snmp_shutdown(AGNT_NAME);
    system("sysevent set snmp_subagent-status stopped");
    SOCK_CLEANUP;

    return 0;
}
