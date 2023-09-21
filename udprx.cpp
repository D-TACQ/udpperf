/** Copyright (C) 2016 - 2020 European Spallation Source ERIC */
#define __STDC_FORMAT_MACROS 1

#include <CLI/CLI.hpp>
#include <cassert>
#include <inttypes.h>
#include <iostream>
#include <common/Socket.h>
#include <common/Timer.h>
#include <stdio.h>

struct {
  int UDPPort{9000};
  int DataSize{9000};
  int SocketBufferSize{2000000};
  int SamplesPerPacket{1};
  int SampleSizeBytes{64};
  int CountColumn{1};
} Settings;

CLI::App app{"UDP receiver with 32 bit sequence number check."};

int main(int argc, char *argv[]) {
  app.add_option("-p, --port", Settings.UDPPort, "UDP receive port");
  //app.add_option("-s, --size", Settings.DataSize, "User data size");
  app.add_option("-b, --socket_buffer_size", Settings.SocketBufferSize, "socket buffer size (bytes)");
  app.add_option("--spp", Settings.SamplesPerPacket, "Samples per packet");
  app.add_option("--ssb", Settings.SampleSizeBytes, "Sample size (bytes)");
  app.add_option("-c, --count_column", Settings.CountColumn, "Count column (indexed from 0)");
  CLI11_PARSE(app, argc, argv);

  static const int BUFFERSIZE{9200};
  char buffer[BUFFERSIZE];
  uint64_t RxBytesTotal{0};
  uint64_t RxBytes{0};
  uint64_t RxPackets{0};
  uint32_t SeqNo{0};
  uint32_t SpadTracker{1};
  uint32_t SpadCount{1};
  int ErrCount{0};
  int SpadIndex{0};
  const int intervalUs = 1000000;
  const int B1M = 1000000;

  Socket::Endpoint local("0.0.0.0", Settings.UDPPort);
  UDPReceiver Receive(local);
  Receive.setBufferSizes(Settings.SocketBufferSize, Settings.SocketBufferSize);
  Receive.printBufferSizes();

  Settings.DataSize = Settings.SampleSizeBytes*Settings.SamplesPerPacket;

  Timer UpdateTimer;
  auto USecs = UpdateTimer.timeus();

  for (;;) {
    int ReadSize = Receive.receive(buffer, BUFFERSIZE);

    assert(ReadSize > 0);
    //printf("ReadSize = %d\n",ReadSize);fflush(stdout);
    assert(ReadSize == Settings.DataSize);

    if (ReadSize > 0) {
      SeqNo = *((uint32_t *)buffer);
      RxBytes += ReadSize;
      RxPackets++;
    }

    // Grab the first SPAD Count in case we're not starting from 1
    if (RxPackets == 1) {
        SpadTracker = *((uint32_t *)( buffer + 4*Settings.CountColumn ));
    }

    for (int i = 0; i <= Settings.SamplesPerPacket-1; i++) {
        SpadIndex = ( i*Settings.SampleSizeBytes+4*Settings.CountColumn );

        SpadCount = *((uint32_t *)( buffer + SpadIndex ));
        if (RxPackets <= 5) {
            //printf("%#010x    %i\n",SpadCount,SpadCount);fflush(stdout);
        }
        if (SpadTracker != SpadCount) {
            ErrCount = ErrCount + 1;
            printf("Deviation! ErrCount = %i, Expected SPAD : %i, Received SPAD : %i, Completed Packets = %li, Sample Jump = %i\n", ErrCount, SpadTracker, SpadCount, RxPackets-1, (Settings.SampleSizeBytes * (SpadCount - SpadTracker)) );fflush(stdout);
            SpadTracker = SpadCount; // Ignore error, reinitialise tracker variable
            if (ErrCount > 9) {
                abort();
            }
        }
        SpadTracker = SpadTracker + 1;
    }

    if ((RxPackets % 100) == 0)
      USecs = UpdateTimer.timeus();

    if (USecs >= intervalUs) {
      RxBytesTotal += RxBytes;
      printf("Rx rate: %.2f Mbps, rx %" PRIu64 " MB (total: %" PRIu64
             " MB) %" PRIu64 " usecs, PER %4.3e\n",
             RxBytes * 8.0 / (USecs / 1000000.0) / B1M, RxBytes / B1M, RxBytesTotal / B1M,
             USecs, (1.0 * ErrCount / RxPackets));
      RxBytes = 0;
      UpdateTimer.now();
      USecs = UpdateTimer.timeus();
    }
  }
}
