# syntax=docker/dockerfile:1

# Create a basic stage set up to build APKs
FROM alpine:3.20 as alpine-builder
RUN apk add \
		--update-cache \
		abuild \
		alpine-conf \
		alpine-sdk \
	&& setup-apkcache /var/cache/apk \
	&& mkdir -p /pkgs/apk \
	&& echo 'builder ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers
# 核心修改：在创建用户和密钥后，将公钥复制到 apk 的信任目录中
# 这解决了 "UNTRUSTED signature" 的错误
RUN adduser -D -G abuild builder \
    && su builder -c 'abuild-keygen -a -n' \
    && cp /home/builder/.abuild/*.pub /etc/apk/keys/

# This stage builds an APK for libyang
FROM alpine-builder as alpine-apk-builder-libyang

# 设置编译级别为 -O0 (无优化)，以最快速度完成编译
ENV CFLAGS="-O0 -pipe"
ENV CXXFLAGS="-O0 -pipe"


RUN mkdir -p /src/libyang
COPY docker/alpine/libyang/APKBUILD /src/libyang
RUN chown -R builder /pkgs /src
USER builder
RUN cd /src/libyang \
	&& abuild checksum \
	&& git init \
	&& abuild -r -P /pkgs/apk

# This stage builds a dist tarball from the source
FROM alpine:3.20 as source-builder

# 设置编译级别为 -O0 (无优化)，以最快速度完成编译
ENV CFLAGS="-O0 -pipe"
ENV CXXFLAGS="-O0 -pipe"


RUN apk add --no-cache --update-cache wget bash \
    && wget --no-check-certificate -O - https://cdn.jsdelivr.net/gh/SuperManito/LinuxMirrors@main/ChangeMirrors.sh | bash -s -- \
        --source mirrors.tuna.tsinghua.edu.cn --protocol http --use-intranet-source false \
        --backup true --upgrade-software false --clean-cache false --ignore-backup-tips

RUN mkdir -p /src/alpine /pkgs/apk
COPY alpine/APKBUILD.in /src/alpine
COPY --from=alpine-apk-builder-libyang /pkgs/apk/src /pkgs/apk
RUN cd /pkgs/apk && apk add --allow-untrusted */*.apk
RUN source /src/alpine/APKBUILD.in \
	&& apk add \
		--no-cache \
		--update-cache \
		$makedepends
COPY . /src
ARG PKGVER
RUN cd /src \
	&& ./bootstrap.sh \
	&& ./configure \
		--enable-numeric-version \
		--with-pkg-extra-version="_git$PKGVER" \
	&& make dist

# This stage builds an APK from the dist tarball
FROM alpine-builder as alpine-apk-builder

# 设置编译级别为 -O0 (无优化)，以最快速度完成编译
ENV CFLAGS="-O0 -pipe"
ENV CXXFLAGS="-O0 -pipe"

COPY --from=source-builder /src/frr-*.tar.gz /src/alpine/* /dist/
COPY --from=alpine-apk-builder-libyang /pkgs/apk/src /pkgs/apk
RUN cd /pkgs/apk && apk add --allow-untrusted */*.apk
RUN find /pkgs/apk -type f -name APKINDEX.tar.gz -delete
RUN chown -R builder /dist /pkgs
USER builder
RUN cd /dist \
	&& abuild checksum \
	&& git init \
	&& abuild -r -P /pkgs/apk

# This stage installs frr from the apk
FROM alpine:3.20
RUN mkdir -p /pkgs/apk
COPY --from=alpine-apk-builder /pkgs/apk/ /pkgs/apk/
RUN apk add \
		--no-cache \
		--update-cache \
		tini \
	&& apk add \
		--no-cache \
		--allow-untrusted /pkgs/apk/*/*.apk \
	&& rm -rf /pkgs

# Own the config / PID files
RUN mkdir -p /var/run/frr
RUN chown -R frr:frr /etc/frr /var/run/frr

# Simple init manager for reaping processes and forwarding signals
ENTRYPOINT ["/sbin/tini", "--"]

# Default CMD starts watchfrr
COPY docker/alpine/docker-start /usr/lib/frr/docker-start
CMD ["/usr/lib/frr/docker-start"]