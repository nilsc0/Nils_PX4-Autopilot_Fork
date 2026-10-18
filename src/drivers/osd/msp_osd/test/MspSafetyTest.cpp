/****************************************************************************
 *
 *   Copyright (c) 2022 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#include <drivers/drv_hrt.h>
#include "uorb_to_msp.hpp"
#include "MspV1.hpp"
#include <gtest/gtest.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <vector>
#include <cerrno>
#include <cstring>
#include <limits>

// Deterministic clock for this standalone host test executable only.
extern "C" hrt_abstime hrt_absolute_time() { return 2000000; }

static sensor_gps_s gps_sample()
{
	sensor_gps_s gps{};
	gps.timestamp = 1900000;
	gps.fix_type = 3;
	gps.vel_ned_valid = true;
	gps.vel_m_s = 10.f;
	return gps;
}

static airspeed_validated_s air_sample()
{
	airspeed_validated_s air{};
	air.timestamp = 1900000;
	air.airspeed_source = airspeed_validated_s::SOURCE_SENSOR_1;
	air.indicated_airspeed_m_s = 20.f;
	return air;
}

TEST(MspSpeed, DistinctSourcesAndUnits)
{
	auto gps = gps_sample();
	auto air = air_sample();
	EXPECT_STREQ(msp_osd::construct_displayport_ground_speed(gps).str, "GS  36.0km/h");
	EXPECT_STREQ(msp_osd::construct_displayport_airspeed(air).str, "IAS  72.0km/h");
	EXPECT_EQ(msp_osd::construct_RAW_GPS(gps).groundSpeed, 1000);
	gps.vel_m_s = 0.f;
	air.indicated_airspeed_m_s = 0.f;
	EXPECT_STREQ(msp_osd::construct_displayport_ground_speed(gps).str, "GS   0.0km/h");
	EXPECT_STREQ(msp_osd::construct_displayport_airspeed(air).str, "IAS   0.0km/h");
}

TEST(MspSpeed, MissingStaleAndFutureSamples)
{
	for (uint64_t timestamp : {0ULL, 1000000ULL, 2000001ULL}) {
		auto gps = gps_sample(); gps.timestamp = timestamp;
		auto air = air_sample(); air.timestamp = timestamp;
		EXPECT_NE(strstr(msp_osd::construct_displayport_ground_speed(gps).str, "---"), nullptr);
		EXPECT_NE(strstr(msp_osd::construct_displayport_airspeed(air).str, "---"), nullptr);
		EXPECT_EQ(msp_osd::construct_RAW_GPS(gps).groundSpeed, 0);
	}
}

TEST(MspSpeed, InvalidValues)
{
	for (float speed : {-1.f, NAN, INFINITY}) {
		auto gps = gps_sample(); gps.vel_m_s = speed;
		auto air = air_sample(); air.indicated_airspeed_m_s = speed;
		EXPECT_NE(strstr(msp_osd::construct_displayport_ground_speed(gps).str, "---"), nullptr);
		EXPECT_NE(strstr(msp_osd::construct_displayport_airspeed(air).str, "---"), nullptr);
		EXPECT_EQ(msp_osd::construct_RAW_GPS(gps).groundSpeed, 0);
	}
	auto gps = gps_sample(); gps.fix_type = 1;
	EXPECT_NE(strstr(msp_osd::construct_displayport_ground_speed(gps).str, "---"), nullptr);
	gps.fix_type = 3; gps.vel_ned_valid = false;
	EXPECT_NE(strstr(msp_osd::construct_displayport_ground_speed(gps).str, "---"), nullptr);
	auto air = air_sample(); air.airspeed_source = airspeed_validated_s::SOURCE_DISABLED;
	EXPECT_NE(strstr(msp_osd::construct_displayport_airspeed(air).str, "---"), nullptr);
	air.airspeed_source = airspeed_validated_s::SOURCE_GROUND_MINUS_WIND;
	EXPECT_STREQ(msp_osd::construct_displayport_airspeed(air).str, "IAS  72.0km/h");
}

TEST(MspSpeed, SaturatesWithoutIntegerOverflow)
{
	auto gps = gps_sample(); gps.vel_m_s = std::numeric_limits<float>::max();
	EXPECT_EQ(msp_osd::construct_RAW_GPS(gps).groundSpeed, 32767);
	EXPECT_STREQ(msp_osd::construct_displayport_ground_speed(gps).str, "GS 9999.9km/h");
}

TEST(MspSpeed, ActualDisplayPortPacket)
{
	int fds[2]; ASSERT_EQ(pipe(fds), 0);
	MspV1 sender(fds[1]);

	for (auto msg : {
		     msp_osd::construct_displayport_ground_speed(gps_sample()),
		     msp_osd::construct_displayport_airspeed(air_sample())
	     }) {
		const size_t payload_size = 4 + strlen(msg.str);
		ASSERT_TRUE(sender.Send(MSP_CMD_DISPLAYPORT, &msg, payload_size));
		uint8_t packet[64] {};
		ASSERT_EQ(read(fds[0], packet, sizeof(packet)), static_cast<ssize_t>(payload_size + 6));
		EXPECT_EQ(memcmp(packet, "$M>", 3), 0);
		EXPECT_EQ(packet[3], payload_size);
		EXPECT_EQ(packet[4], MSP_CMD_DISPLAYPORT);
		EXPECT_EQ(packet[5], 3);
		EXPECT_EQ(packet[6], msg.screenYPosition);
		EXPECT_EQ(packet[7], 2);
		EXPECT_EQ(packet[8], 0);
		EXPECT_EQ(memcmp(packet + 9, msg.str, strlen(msg.str)), 0);
		uint8_t crc = 0;

		for (size_t i = 3; i < payload_size + 5; ++i) { crc ^= packet[i]; }

		EXPECT_EQ(packet[payload_size + 5], crc);
	}
	close(fds[0]); close(fds[1]);
}

TEST(MspTransport, MaximumPayloadReceive)
{
	int fds[2]; ASSERT_EQ(pipe(fds), 0);
	uint8_t frame[261] {};
	memcpy(frame, "$M<", 3);
	frame[3] = 255; frame[4] = 1;
	frame[260] = 254;
	ASSERT_EQ(write(fds[1], frame, sizeof(frame)), static_cast<ssize_t>(sizeof(frame)));
	MspV1 receiver(fds[0]);
	uint8_t payload[255] {};
	uint8_t id = 0;
	EXPECT_EQ(receiver.Receive(payload, &id, sizeof(payload)), 255);
	EXPECT_EQ(id, 1);
	close(fds[0]); close(fds[1]);
}

static std::vector<uint8_t> rx_frame(size_t length)
{
	std::vector<uint8_t> frame(length + 6);
	memcpy(frame.data(), "$M<", 3);
	frame[3] = length; frame[4] = 1;
	uint8_t crc = length ^ 1;

	for (size_t i = 0; i < length; ++i) { frame[5 + i] = i; crc ^= frame[5 + i]; }

	frame[5 + length] = crc;
	return frame;
}

TEST(MspTransport, AllPayloadLengths)
{
	int fds[2]; ASSERT_EQ(pipe(fds), 0);
	MspV1 receiver(fds[0]);

	for (size_t length = 0; length <= 255; ++length) {
		const auto frame = rx_frame(length);
		ASSERT_EQ(write(fds[1], frame.data(), frame.size()), static_cast<ssize_t>(frame.size()));
		uint8_t payload[255] {}; uint8_t id = 0;
		ASSERT_EQ(receiver.Receive(payload, &id, sizeof(payload)), static_cast<int>(length));
		EXPECT_EQ(id, 1);
		EXPECT_EQ(memcmp(payload, frame.data() + 5, length), 0);
	}

	close(fds[0]); close(fds[1]);
}

TEST(MspTransport, RejectsUndersizedBuffer)
{
	int fds[2]; ASSERT_EQ(pipe(fds), 0);
	const auto frame = rx_frame(255);
	ASSERT_EQ(write(fds[1], frame.data(), frame.size()), static_cast<ssize_t>(frame.size()));
	MspV1 receiver(fds[0]);
	uint8_t payload[1] {42}; uint8_t id = 99;
	EXPECT_EQ(receiver.Receive(payload, &id, sizeof(payload)), -EMSGSIZE);
	EXPECT_EQ(payload[0], 42); EXPECT_EQ(id, 99);
	close(fds[0]); close(fds[1]);
}

TEST(MspTransport, FragmentedPayloadWaitsWithoutDecodingPartialData)
{
	int fds[2]; ASSERT_EQ(pipe(fds), 0);
	const auto frame = rx_frame(10);
	ASSERT_EQ(write(fds[1], frame.data(), 8), 8);
	MspV1 receiver(fds[0]);
	uint8_t payload[255] {42}; uint8_t id = 99;
	EXPECT_EQ(receiver.Receive(payload, &id, sizeof(payload)), -EWOULDBLOCK);
	EXPECT_EQ(payload[0], 42); EXPECT_EQ(id, 99);
	ASSERT_EQ(write(fds[1], frame.data() + 8, frame.size() - 8), static_cast<ssize_t>(frame.size() - 8));
	EXPECT_EQ(receiver.Receive(payload, &id, sizeof(payload)), 10);
	EXPECT_EQ(memcmp(payload, frame.data() + 5, 10), 0);
	close(fds[0]); close(fds[1]);
}

TEST(MspTransport, BadCrcAndNoiseDoNotProduceMessages)
{
	int fds[2]; ASSERT_EQ(pipe(fds), 0);
	auto frame = rx_frame(10); frame.back() ^= 1;
	ASSERT_EQ(write(fds[1], frame.data(), frame.size()), static_cast<ssize_t>(frame.size()));
	MspV1 receiver(fds[0]);
	uint8_t payload[255] {}; uint8_t id = 99;
	EXPECT_EQ(receiver.Receive(payload, &id, sizeof(payload)), -EINVAL);
	EXPECT_EQ(id, 99);
	uint8_t noise[100] {};
	ASSERT_EQ(write(fds[1], noise, sizeof(noise)), static_cast<ssize_t>(sizeof(noise)));
	EXPECT_EQ(receiver.Receive(payload, &id, sizeof(payload)), -EWOULDBLOCK);
	int remaining = 0; ASSERT_EQ(ioctl(fds[0], FIONREAD, &remaining), 0);
	EXPECT_EQ(remaining, 68);
	frame = rx_frame(10);
	ASSERT_EQ(write(fds[1], frame.data(), frame.size()), static_cast<ssize_t>(frame.size()));
	EXPECT_EQ(receiver.Receive(payload, &id, sizeof(payload)), -EWOULDBLOCK);
	EXPECT_EQ(receiver.Receive(payload, &id, sizeof(payload)), -EWOULDBLOCK);
	EXPECT_EQ(receiver.Receive(payload, &id, sizeof(payload)), 10);
	close(fds[0]); close(fds[1]);
}

TEST(MspTransport, BoundedSendAndEmptyPayload)
{
	int fds[2]; ASSERT_EQ(pipe(fds), 0);
	MspV1 sender(fds[1]);
	uint8_t payload[256] {};
	EXPECT_FALSE(sender.Send(1, payload, 256));
	EXPECT_FALSE(sender.Send(1, nullptr, 1));
	EXPECT_TRUE(sender.Send(1, nullptr, 0));
	uint8_t packet[261] {};
	ASSERT_EQ(read(fds[0], packet, sizeof(packet)), 6);
	EXPECT_EQ(packet[3], 0); EXPECT_EQ(packet[5], 1);
	EXPECT_TRUE(sender.Send(1, payload, 255));
	ASSERT_EQ(read(fds[0], packet, sizeof(packet)), 261);
	EXPECT_EQ(packet[3], 255); EXPECT_EQ(packet[260], 254);
	close(fds[0]); close(fds[1]);
}
