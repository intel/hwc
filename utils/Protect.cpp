/*
// Copyright (c) 2017 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "HwcServiceApi.h"

using namespace std;

int main(int argc, char** argv)
{

    // Argument parameters
    bool bEnable  = false;
    bool bDisableAll = false;
    int sessionID = 0;
    int instanceID = 0;
    int argsRequired = 2;

    if (argc >= 2)
    {
        if (strcmp(argv[1], "on") == 0)
        {
            bEnable = true;
            argsRequired = 4;
        }
        else if (strcmp(argv[1], "off") == 0)
        {
            bEnable = false;
            argsRequired = 3;
        }
        else if (strcmp(argv[1], "alloff") == 0)
        {
            bDisableAll = true;
        }
        if ( argc > 2 )
        {
            sessionID = atoi( argv[2] );
        }
        if ( argc > 3 )
        {
            instanceID = atoi( argv[3] );
        }
    }
    if (argc < argsRequired)
    {
        printf("Usage: %s on {session} {instance}\n", argv[0]);
        printf("Usage: %s off {session}\n", argv[0]);
        printf("Usage: %s alloff\n", argv[0]);
        return 1;
    }

    HWCSHANDLE hwcs = HwcService_Connect();
    if(hwcs == NULL) {
        printf("Could not connect to service\n");
        return 1;
    }

    else if (bDisableAll)
    {
        printf("disableAllEncryptedSessions( )\n" );
        HwcService_Video_DisableAllEncryptedSessions( hwcs );
    }
    else if (bEnable)
    {
        printf("enableEncryptedSession( Session:%d, Instance:%d )\n", sessionID, instanceID );
        HwcService_Video_EnableEncryptedSession( hwcs, sessionID, instanceID );
    }
    else
    {
        printf("disableEncryptedSession( Session:%d )\n", sessionID );
        HwcService_Video_DisableEncryptedSession( hwcs, sessionID );
    }

    HwcService_Disconnect(hwcs);

    return 0;
}
