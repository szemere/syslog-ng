FROM debian:jessie
ARG DISTRO=jessie
ARG OBS_REPO=Debian_8.0

LABEL maintainer="Andras Mitzki <andras.mitzki@balabit.com>, Laszlo Szemere <laszlo.szemere@balabit.com>"

# Install packages
RUN apt-get update -qq

RUN ls -hal /

RUN apt-get install --no-install-recommends -y python-pip

RUN ls -hal /

COPY helpers/* /helpers/

RUN ls -hal /

RUN apt-get install --no-install-recommends -y python-setuptools

RUN ls -hal /

RUN apt-get install --no-install-recommends -y wget

RUN ls -hal /

COPY required-pip/all.txt required-pip/${DISTRO}*.txt /required-pip/
RUN cat /required-pip/* | grep -v '^$\|^#' | xargs pip install

RUN ls -hal /

COPY required-apt/all.txt required-apt/${DISTRO}*.txt /required-apt/
RUN cat /required-apt/* | grep -v '^$\|^#' | xargs apt-get install --no-install-recommends -y

RUN ls -hal /

RUN /helpers/functions.sh add_obs_repo ${OBS_REPO}
COPY required-obs/all.txt required-obs/${DISTRO}*.txt /required-obs/
RUN cat /required-obs/* | grep -v '^$\|^#' | xargs apt-get install --no-install-recommends -y

RUN ls -hal /

# grab gosu for easy step-down from root
RUN /helpers/functions.sh step_down_from_root_with_gosu $(dpkg --print-architecture)

RUN ls -hal /

# add a fake sudo command
RUN mv /helpers/fake-sudo.sh /usr/bin/sudo

RUN ls -hal /

# mount points for source code
RUN mkdir /source
VOLUME /source
VOLUME /build

RUN ls -hal /

ENTRYPOINT ["/helpers/entrypoint.sh"]

RUN ls -hal /
