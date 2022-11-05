ARG DOCKER_IMAGE=alpine:3.14
FROM $DOCKER_IMAGE AS builder

ENV MINETEST_GAME_VERSION master
ENV IRRLICHT_VERSION master

COPY mods /usr/src/minetest/mods
COPY .git /usr/src/minetest/.git
COPY CMakeLists.txt /usr/src/minetest/CMakeLists.txt
COPY README.md /usr/src/minetest/README.md
COPY freeminer.conf.example /usr/src/minetest/freeminer.conf.example
COPY builtin /usr/src/minetest/builtin
COPY cmake /usr/src/minetest/cmake
COPY doc /usr/src/minetest/doc
COPY fonts /usr/src/minetest/fonts
COPY lib /usr/src/minetest/lib
COPY misc /usr/src/minetest/misc
COPY po /usr/src/minetest/po
COPY src /usr/src/minetest/src
COPY textures /usr/src/minetest/textures

WORKDIR /usr/src/minetest

RUN apk add --no-cache git build-base cmake sqlite-dev curl-dev zlib-dev zstd-dev \
                msgpack-c boost-dev leveldb-dev snappy-dev libunwind-dev libexecinfo-dev \
		gmp-dev jsoncpp-dev postgresql-dev ninja luajit-dev ca-certificates && \
true
#	git clone --depth=1 -b ${MINETEST_GAME_VERSION} https://github.com/minetest/minetest_game.git ./games/minetest_game && \
#	rm -fr ./games/minetest_game/.git

WORKDIR /usr/src/
RUN git clone --recursive https://github.com/jupp0r/prometheus-cpp/ && \
	cd prometheus-cpp && \
	cmake -B build \
		-DCMAKE_INSTALL_PREFIX=/usr/local \
		-DCMAKE_BUILD_TYPE=Release \
		-DENABLE_TESTING=0 \
		-GNinja && \
	cmake --build build && \
	cmake --install build

RUN git clone --depth=1 https://github.com/minetest/irrlicht/ -b ${IRRLICHT_VERSION} && \
	cp -r irrlicht/include /usr/include/irrlichtmt

WORKDIR /usr/src/minetest
RUN cmake -B build \
        -DRUN_IN_PLACE=0 \
		-DCMAKE_INSTALL_PREFIX=/usr/local \
		-DCMAKE_BUILD_TYPE=Release \
		-DBUILD_SERVER=TRUE \
		-DENABLE_PROMETHEUS=TRUE \
		-DBUILD_UNITTESTS=FALSE \
		-DBUILD_CLIENT=FALSE \
		-GNinja && \
	cmake --build build && \
	cmake --install build

ARG DOCKER_IMAGE=alpine:3.14
FROM $DOCKER_IMAGE AS runtime

RUN apk add --no-cache sqlite-libs curl gmp libstdc++ libgcc libpq luajit jsoncpp zstd-libs && \
	adduser -D minetest --uid 30000 -h /var/lib/minetest && \
	chown -R minetest:minetest /var/lib/minetest

WORKDIR /var/lib/minetest

COPY --from=builder /usr/local/share/freeminer /usr/local/share/freeminer
COPY --from=builder /usr/local/bin/freeminerserver /usr/local/bin/freeminerserver
COPY --from=builder /usr/local/share/doc/freeminer/freeminer.conf.example /etc/freeminer/freeminer.conf

USER minetest:minetest

EXPOSE 30000/udp 30000/tcp

CMD ["/usr/local/bin/freeminerserver", "--config", "/etc/freeminer/freeminer.conf"]
