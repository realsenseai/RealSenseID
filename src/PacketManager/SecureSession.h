// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "SerialConnection.h"
#include "SerialPacket.h"
#include "CommonTypes.h"
#include "Timer.h"
#include "MbedtlsWrapper.h"
#include <atomic>

// Thread safe session manager. sends/receive packets with encryption.
// Session starts on Start(serial_connection*) and ends in destruction.
// Note:
// This class is not responsible for opening/closing the serial connection. It only uses the given one.
namespace RealSenseID
{
namespace PacketManager
{
using SignCallback = std::function<bool(const unsigned char*, const unsigned int, unsigned char*)>;
using VerifyCallback = std::function<bool(const unsigned char*, const unsigned int, const unsigned char*, const unsigned int)>;


class SecureSession
{
public:
    SecureSession(SignCallback signCallback, VerifyCallback verifyCallback);
    ~SecureSession();

    SecureSession(const SecureSession&) = delete;
    SecureSession& operator=(const SecureSession&) = delete;

    SerialStatus Pair(SerialConnection* serial_conn, const char* ecdsaHostPubKey, const char* ecdsaHostPubKeySig, char* ecdsaDevicePubKey);
    SerialStatus Unpair(SerialConnection* serial_conn);

    // Start the session using the given (already open) serial connection.
    // return Status::Ok on success, or error Status otherwise.
    SerialStatus Start(SerialConnection* serial_conn);

    // return true if session is open
    bool IsOpen() const;

    // cancel may be called from different threads
    std::atomic<bool> _cancel_required {false};

    // Send packet
    // return Status::Ok on success, or error status otherwise.
    SerialStatus SendPacket(SerialPacket& packet);

    // Wait for any packet until timeout.
    // Fill the given packet with the received packet.
    // return Status::Ok on success, or error status otherwise.
    SerialStatus RecvPacket(SerialPacket& packet);

    // Wait for fa packet until default timeout.
    // Fill the given packet with the received fa packet.
    // If no fa packet available, return timeout status.
    // If the wrong packet type arrives, return RecvUnexpectedPacket status.
    // return Status::Ok on success, or error status otherwise.
    SerialStatus RecvFaPacket(FaPacket& packet);

    // Wait for fa packet until given timeout.
    // Fill the given packet with the received fa packet.
    // If no fa packet available, return timeout status.
    // If the wrong packet type arrives, return RecvUnexpectedPacket status.
    // return Status::Ok on success, or error status otherwise.
    SerialStatus RecvFaPacket(FaPacket& packet, timeout_t timeout);

    // Wait for data packet until timeout.
    // Fill the given packet with the received data packet.
    // If no data packet available, return timeout status.
    // If the wrong packet type arrives, return RecvUnexpectedPacket status.
    // return Status::Ok on success, or error status otherwise.
    SerialStatus RecvDataPacket(DataPacket& packet);

    // async cancel. set the _cancel_required flag and send cancel before next recv
    void Cancel();

    // sync cancel. send the cancel message now
    SerialStatus SendCancel();

    // notify that the connection was closed
    void OnConnectionClosed(); // call after the connection is closed/reset

private:
    SerialConnection* _serial = nullptr;
    uint32_t _last_sent_seq_number = 0;
    uint32_t _last_recv_seq_number = 0;
    SignCallback _sign_callback;
    VerifyCallback _verify_callback;
    MbedtlsWrapper _crypto_wrapper;
    bool _is_open = false;

    SerialStatus PairImpl(SerialConnection* serial_conn, const char* ecdsaHostPubKey, const char* ecdsaHostPubKeySig,
                          char* ecdsaDevicePubKey);
    SerialStatus SendPacketImpl(SerialPacket& packet);
    SerialStatus RecvPacketImpl(SerialPacket& packet, timeout_t recv_timeout);
    SerialStatus HandleCancelFlag(); // if _cancel_required, send cancel. otherwise do nothing
};
} // namespace PacketManager
} // namespace RealSenseID
