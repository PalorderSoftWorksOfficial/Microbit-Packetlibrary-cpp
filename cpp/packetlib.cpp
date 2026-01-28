#include "headers/packetlib.h"
#include "pxt.h"
#include "pxtmicro.h"

namespace PacketLib {

    static String localId = microbit_serial_number();
    static ActionWithArg<Packet> receiveHandler;
    static Vector<int> pendingAcks;

    // ===== Utility =====
    int generatePacketId() {
        return rand() % 65535 + 1;
    }

    int computeChecksum(Buffer buf) {
        int sum = 0;
        for (int i = 0; i < buf->length; i++) {
            sum ^= buf->data[i];
        }
        return sum;
    }

    void copyBuffer(Buffer src, Buffer dst, int offset) {
        for (int i = 0; i < src->length; i++) {
            dst->data[offset + i] = src->data[i];
        }
    }

    int writeString(Buffer buf, int offset, String value) {
        buf->data[offset] = value.length();
        for (int i = 0; i < value.length(); i++) {
            buf->data[offset + 1 + i] = value.charAt(i);
        }
        return 1 + value.length();
    }

    String readString(Buffer buf, int offset, int &size) {
        int len = buf->data[offset];
        String s = "";
        for (int i = 0; i < len; i++) {
            s += (char)buf->data[offset + 1 + i];
        }
        size = 1 + len;
        return s;
    }

    // ===== Encode / Decode =====
    Buffer encodePacket(int id, String destination, Buffer payload, int flags) {
        if (id == 0) id = generatePacketId();

        int baseSize = 1 + 1 + 2 + 1 + localId.length() + 1 + destination.length() + 1 + payload->length + 1;
        Buffer buf = mkBuffer(baseSize);
        int offset = 0;

        buf->data[offset++] = PROTOCOL_VERSION;
        buf->data[offset++] = flags;
        buf->setUInt16LE(offset, id);
        offset += 2;

        offset += writeString(buf, offset, localId);
        offset += writeString(buf, offset, destination);

        buf->data[offset++] = payload->length;
        copyBuffer(payload, buf, offset);
        offset += payload->length;

        buf->data[offset] = computeChecksum(buf);
        return buf;
    }

    Packet decodePacket(Buffer buf) {
        Packet pkt;
        int offset = 0;

        pkt.version = buf->data[offset++];
        pkt.flags = buf->data[offset++];
        pkt.id = buf->getUInt16LE(offset);
        offset += 2;

        int sz;
        pkt.source = readString(buf, offset, sz);
        offset += sz;

        pkt.destination = readString(buf, offset, sz);
        offset += sz;

        int payloadLen = buf->data[offset++];
        pkt.payload = buf->slice(offset, payloadLen);

        return pkt;
    }

    // ===== Public API =====
    void sendPacket(int id, Buffer payload, String destination, int flags) {
        Buffer pkt = encodePacket(id, destination, payload, flags);
        uBit.radio.send(pkt);

        if (flags & FLAG_ACK_REQUIRED) {
            pendingAcks.push_back(id);
        }
    }

    void onReceivePacket(ActionWithArg<Packet> handler) {
        receiveHandler = handler;
    }

    // ===== Buffer to String =====
    String bufferToString(Buffer buf) {
        String s = "";
        for (int i = 0; i < buf->length; i++) {
            s += (char)buf->data[i];
        }
        return s;
    }

    // ===== Radio Handler =====
    static void radioBufferHandler(Buffer buf) {
        int checksum = buf->data[buf->length - 1];
        if (computeChecksum(buf) != checksum)
            return;

        Packet pkt = decodePacket(buf);

        if (pkt.flags & FLAG_IS_ACK) {
            for (int i = 0; i < pendingAcks.size(); i++) {
                if (pendingAcks[i] == pkt.id) {
                    pendingAcks.erase(pendingAcks.begin() + i);
                    break;
                }
            }
            return;
        }

        if (pkt.flags & FLAG_ACK_REQUIRED) {
            Buffer ack = encodePacket(pkt.id, pkt.source, mkBuffer(0), FLAG_IS_ACK);
            uBit.radio.send(ack);
        }

        if (receiveHandler)
            receiveHandler(pkt);
    }

    // Auto-register radio handler
    struct Init {
        Init() {
            uBit.radio.onReceivedBuffer(radioBufferHandler);
        }
    } _init;

}
