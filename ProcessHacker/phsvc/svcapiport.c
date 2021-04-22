/*
 * Process Hacker -
 *   server API port
 *
 * Copyright (C) 2011-2015 wj32
 *
 * This file is part of Process Hacker.
 *
 * Process Hacker is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Process Hacker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Process Hacker.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <phapp.h>
#include <phsvc.h>

NTSTATUS PhSvcApiRequestThreadStart(
    _In_ PVOID Parameter
    );

extern HANDLE PhSvcTimeoutStandbyEventHandle;
extern HANDLE PhSvcTimeoutCancelEventHandle;

ULONG PhSvcApiThreadContextTlsIndex;
HANDLE PhSvcApiPortHandle;
ULONG PhSvcApiNumberOfClients = 0;

NTSTATUS PhSvcApiPortInitialization(
    _In_ PUNICODE_STRING PortName
    )
{
    static SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    NTSTATUS status;
    OBJECT_ATTRIBUTES objectAttributes;
    PSECURITY_DESCRIPTOR securityDescriptor;
    ULONG sdAllocationLength;
    UCHAR administratorsSidBuffer[FIELD_OFFSET(SID, SubAuthority) + sizeof(ULONG) * 2];
    PSID administratorsSid;
    PACL dacl;
    ULONG i;

    // Create the API port.

    administratorsSid = (PSID)administratorsSidBuffer;
    RtlInitializeSid(administratorsSid, &ntAuthority, 2);
    *RtlSubAuthoritySid(administratorsSid, 0) = SECURITY_BUILTIN_DOMAIN_RID;
    *RtlSubAuthoritySid(administratorsSid, 1) = DOMAIN_ALIAS_RID_ADMINS;

    sdAllocationLength = SECURITY_DESCRIPTOR_MIN_LENGTH +
        (ULONG)sizeof(ACL) +
        (ULONG)sizeof(ACCESS_ALLOWED_ACE) +
        RtlLengthSid(administratorsSid) +
        (ULONG)sizeof(ACCESS_ALLOWED_ACE) +
        RtlLengthSid(&PhSeEveryoneSid);

    securityDescriptor = PhAllocate(sdAllocationLength);
    dacl = (PACL)PTR_ADD_OFFSET(securityDescriptor, SECURITY_DESCRIPTOR_MIN_LENGTH);

    RtlCreateSecurityDescriptor(securityDescriptor, SECURITY_DESCRIPTOR_REVISION);
    RtlCreateAcl(dacl, sdAllocationLength - SECURITY_DESCRIPTOR_MIN_LENGTH, ACL_REVISION);
    RtlAddAccessAllowedAce(dacl, ACL_REVISION, PORT_ALL_ACCESS, administratorsSid);
    RtlAddAccessAllowedAce(dacl, ACL_REVISION, PORT_CONNECT, &PhSeEveryoneSid);
    RtlSetDaclSecurityDescriptor(securityDescriptor, TRUE, dacl, FALSE);

    InitializeObjectAttributes(
        &objectAttributes,
        PortName,
        OBJ_CASE_INSENSITIVE,
        NULL,
        securityDescriptor
        );

    status = NtCreatePort(
        &PhSvcApiPortHandle,
        &objectAttributes,
        sizeof(PHSVC_API_CONNECTINFO),
        PhIsExecutingInWow64() ? sizeof(PHSVC_API_MSG64) : sizeof(PHSVC_API_MSG),
        0
        );
    PhFree(securityDescriptor);

    if (!NT_SUCCESS(status))
        return status;

    // Start the API threads.

    PhSvcApiThreadContextTlsIndex = TlsAlloc();

    for (i = 0; i < 2; i++)
    {
        PhCreateThread2(PhSvcApiRequestThreadStart, NULL);
    }

    return status;
}

PPHSVC_THREAD_CONTEXT PhSvcGetCurrentThreadContext(
    VOID
    )
{
    return (PPHSVC_THREAD_CONTEXT)TlsGetValue(PhSvcApiThreadContextTlsIndex);
}

NTSTATUS PhSvcApiRequestThreadStart(
    _In_ PVOID Parameter
    )
{
    PH_AUTO_POOL autoPool;
    NTSTATUS status;
    PHSVC_THREAD_CONTEXT threadContext;
    HANDLE portHandle;
    PVOID portContext;
    SIZE_T messageSize;
    PPORT_MESSAGE receiveMessage;
    PPORT_MESSAGE replyMessage;
    CSHORT messageType;
    PPHSVC_CLIENT client;
    PPHSVC_API_PAYLOAD payload;

    PhInitializeAutoPool(&autoPool);

    threadContext.CurrentClient = NULL;
    threadContext.OldClient = NULL;

    TlsSetValue(PhSvcApiThreadContextTlsIndex, &threadContext);

    portHandle = PhSvcApiPortHandle;
    messageSize = PhIsExecutingInWow64() ? sizeof(PHSVC_API_MSG64) : sizeof(PHSVC_API_MSG);
    receiveMessage = PhAllocate(messageSize);
    replyMessage = NULL;

    while (TRUE)
    {
        status = NtReplyWaitReceivePort(
            portHandle,
            &portContext,
            replyMessage,
            receiveMessage
            );

        portHandle = PhSvcApiPortHandle;
        replyMessage = NULL;

        if (!NT_SUCCESS(status))
        {
            // Client probably died.
            continue;
        }

        messageType = receiveMessage->u2.s2.Type;

        if (messageType == LPC_CONNECTION_REQUEST)
        {
            PhSvcHandleConnectionRequest(receiveMessage);
            continue;
        }

        if (!portContext)
            continue;

        client = portContext;
        threadContext.CurrentClient = client;
        PhWaitForEvent(&client->ReadyEvent, NULL);

        if (messageType == LPC_REQUEST)
        {
            if (PhIsExecutingInWow64())
                payload = &((PPHSVC_API_MSG64)receiveMessage)->p;
            else
                payload = &((PPHSVC_API_MSG)receiveMessage)->p;

            PhSvcDispatchApiCall(client, payload, &portHandle);
            replyMessage = receiveMessage;
        }
        else if (messageType == LPC_PORT_CLOSED)
        {
            PhDereferenceObject(client);

            if (_InterlockedDecrement(&PhSvcApiNumberOfClients) == 0)
            {
                NtSetEvent(PhSvcTimeoutStandbyEventHandle, NULL);
            }
        }

        assert(!threadContext.OldClient);
        PhDrainAutoPool(&autoPool);
    }

    PhDeleteAutoPool(&autoPool);
}

VOID PhSvcHandleConnectionRequest(
    _In_ PPORT_MESSAGE PortMessage
    )
{
    NTSTATUS status;
    PPHSVC_API_MSG message;
    PPHSVC_API_MSG64 message64;
    CLIENT_ID clientId;
    PPHSVC_CLIENT client;
    HANDLE portHandle;
    REMOTE_PORT_VIEW clientView;
    REMOTE_PORT_VIEW64 clientView64;
    PREMOTE_PORT_VIEW actualClientView;

    message = (PPHSVC_API_MSG)PortMessage;
    message64 = (PPHSVC_API_MSG64)PortMessage;

    if (PhIsExecutingInWow64())
    {
        clientId.UniqueProcess = (HANDLE)message64->h.ClientId.UniqueProcess;
        clientId.UniqueThread = (HANDLE)message64->h.ClientId.UniqueThread;
    }
    else
    {
        PPH_STRING referenceFileName;
        PPH_STRING remoteFileName;

        clientId = message->h.ClientId;

        // Make sure that the remote process is Process Hacker itself and not some other program.

        referenceFileName = NULL;
        PhGetProcessImageFileNameByProcessId(NtCurrentProcessId(), &referenceFileName);
        PH_AUTO(referenceFileName);

        remoteFileName = NULL;
        PhGetProcessImageFileNameByProcessId(clientId.UniqueProcess, &remoteFileName);
        PH_AUTO(remoteFileName);

        if (!referenceFileName || !remoteFileName || !PhEqualString(referenceFileName, remoteFileName, TRUE))
        {
            NtAcceptConnectPort(&portHandle, NULL, PortMessage, FALSE, NULL, NULL);
            return;
        }
    }

    client = PhSvcCreateClient(&clientId);

    if (!client)
    {
        NtAcceptConnectPort(&portHandle, NULL, PortMessage, FALSE, NULL, NULL);
        return;
    }

    if (PhIsExecutingInWow64())
    {
        message64->p.ConnectInfo.ServerProcessId = HandleToUlong(NtCurrentProcessId());

        clientView64.Length = sizeof(REMOTE_PORT_VIEW64);
        clientView64.ViewSize = 0;
        clientView64.ViewBase = 0;
        actualClientView = (PREMOTE_PORT_VIEW)&clientView64;
    }
    else
    {
        message->p.ConnectInfo.ServerProcessId = HandleToUlong(NtCurrentProcessId());

        clientView.Length = sizeof(REMOTE_PORT_VIEW);
        clientView.ViewSize = 0;
        clientView.ViewBase = NULL;
        actualClientView = &clientView;
    }

    status = NtAcceptConnectPort(
        &portHandle,
        client,
        PortMessage,
        TRUE,
        NULL,
        actualClientView
        );

    if (!NT_SUCCESS(status))
    {
        PhDereferenceObject(client);
        return;
    }

    // IMPORTANT: Since Vista, NtCompleteConnectPort does not do anything and simply returns STATUS_SUCCESS.
    // We will call it anyway (for completeness), but we need to use an event to ensure that other threads don't try
    // to process requests before we have finished setting up the client object.

    client->PortHandle = portHandle;

    if (PhIsExecutingInWow64())
    {
        client->ClientViewBase = (PVOID)clientView64.ViewBase;
        client->ClientViewLimit = PTR_ADD_OFFSET(clientView64.ViewBase, clientView64.ViewSize);
    }
    else
    {
        client->ClientViewBase = clientView.ViewBase;
        client->ClientViewLimit = PTR_ADD_OFFSET(clientView.ViewBase, clientView.ViewSize);
    }

    NtCompleteConnectPort(portHandle);
    PhSetEvent(&client->ReadyEvent);

    if (_InterlockedIncrement(&PhSvcApiNumberOfClients) == 1)
    {
        NtSetEvent(PhSvcTimeoutCancelEventHandle, NULL);
    }
}
