package org.omachat.protocol

import omachat.proto.Network.*
import org.junit.Assert.*
import org.junit.Test
import java.io.*

class FramingTest {
    @Test fun roundTripUsesBigEndianAndSharedSchema() {
        val envelope = Envelope.newBuilder().setRequestId(42)
            .setLogin(LoginRequest.newBuilder().setUsername("test").setPassword("fixture-only")).build()
        val bytes = ByteArrayOutputStream()
        Framing.write(DataOutputStream(bytes), envelope)
        val encoded = bytes.toByteArray()
        assertEquals(envelope.serializedSize, DataInputStream(ByteArrayInputStream(encoded)).readInt())
        assertEquals(envelope, Framing.read(DataInputStream(ByteArrayInputStream(encoded))))
    }
    @Test fun invalidLengthsRejectedBeforeBodyRead() {
        for (length in listOf(-1, 0, Framing.MAX_BYTES + 1, Int.MAX_VALUE)) {
            val bytes = ByteArrayOutputStream()
            DataOutputStream(bytes).writeInt(length)
            try { Framing.read(DataInputStream(ByteArrayInputStream(bytes.toByteArray()))); fail("accepted length") }
            catch (_: IllegalArgumentException) {}
        }
    }
    @Test(expected = EOFException::class) fun truncatedBodyRejected() {
        Framing.read(DataInputStream(ByteArrayInputStream(byteArrayOf(0, 0, 0, 2, 1))))
    }
    @Test fun additiveSendOperationSurvivesSerialization() {
        val id = com.google.protobuf.ByteString.copyFrom(ByteArray(16) { it.toByte() })
        val request = SendMessageRequest.newBuilder().setChannelId(7).setContent("real wire")
            .setOperationId(id).build()
        assertEquals(id, SendMessageRequest.parseFrom(request.toByteArray()).operationId)
    }
}
