FROM ubuntu:24.04
RUN dpkg --add-architecture i386 \
    && apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        wine wine32:i386 g++ \
    && rm -rf /var/lib/apt/lists/*
ENV WINEDEBUG=-all
ENV WINEPREFIX=/wine
RUN wineboot --init && wineserver --wait
# Native builds of MarkeyJester's helper tools (sources ported from _Assembly Tools/*.c)
COPY CheckFix.cpp CompRom.cpp PatchRom.cpp ListEqu.cpp /tmp/tools/
RUN g++ -O2 -fpermissive -o /usr/local/bin/CheckFix /tmp/tools/CheckFix.cpp \
    && g++ -O2 -fpermissive -o /usr/local/bin/CompRom /tmp/tools/CompRom.cpp \
    && g++ -O2 -fpermissive -o /usr/local/bin/PatchRom /tmp/tools/PatchRom.cpp \
    && g++ -O2 -fpermissive -o /usr/local/bin/ListEqu /tmp/tools/ListEqu.cpp \
    && rm -rf /tmp/tools
WORKDIR /work
