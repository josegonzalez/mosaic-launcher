# The image CI's check and desktop jobs run in, and scripts/docker-test.sh
# runs the tests in locally: the desktop build's libraries, the tools the
# checks use, a virtual display and gdb. CI tags it with a hash of this file,
# so a change here builds a new image on the next run.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# rclone and syncthing are the sync plugin's stand-ins for its bundled ones;
# ffmpeg and sqlite3 make the clips and play history some cases use
RUN apt-get update \
	&& apt-get install -y --no-install-recommends \
		bats \
		build-essential \
		ca-certificates \
		curl \
		ffmpeg \
		file \
		gdb \
		gettext \
		git \
		libavcodec-dev \
		libavformat-dev \
		libavutil-dev \
		libgl-dev \
		libsamplerate0-dev \
		libsdl2-dev \
		libsdl2-image-dev \
		libsdl2-ttf-dev \
		libsqlite3-dev \
		openssl \
		patch \
		pkg-config \
		procps \
		python3 \
		rclone \
		rsync \
		shellcheck \
		sqlite3 \
		syncthing \
		unzip \
		util-linux \
		xauth \
		xvfb \
		zip \
		zlib1g-dev \
	&& rm -rf /var/lib/apt/lists/*

# ip, for bringing up loopback in each plugin test worker's own network
# namespace (tests/plugins/run.sh with JOBS)
RUN apt-get update \
	&& apt-get install -y --no-install-recommends iproute2 \
	&& rm -rf /var/lib/apt/lists/*

# the checkout is owned by another user when it's mounted in
RUN git config --system --add safe.directory '*'
