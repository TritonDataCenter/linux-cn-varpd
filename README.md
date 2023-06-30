# linux-cn-varpd - SVP/varpd for Triton Linux Compute Nodes

As it stands right now, this implementation of an SVP client, which resolves
VXLAN information for Triton Fabric Networks on Linux CNs, assumes that a
different entity (likely
[`net-agent`](https://github.com/TritonDataCenter/sdc-net-agent)) will
configure Linux links in a manner that this implementation can access.

***THIS IS STILL A WORK IN PROGRESS***

## Linux link configuration

### VXLAN

From the bottom up, the first required link is a VXLAN link.  Here is an
example CLI invocation to create:

```
root# ip link add sdcvxl4385813 type vxlan id 4385813 dev net2 dstport 4789 nolearning proxy l2miss l3miss
root# ip link set sdcvxl4385813 up
```

We set `nolearning` because we want Triton's
[`portolan`](https://github.com/TritonDataCenter/sdc-portolan) to be the
source of truth for all Fabric Network mappings. Like the
`sdc_underlay<VNETID>` on illumos, we embed the vnet ID in the link name.

### VLAN

The second required link is a VLAN link. Unlike illumos VNICs, there appears
to be no way to create multiple VLAN links that share the same underlying
link AND the same VLAN id. Also, in order to reduce netlink messages and
obtain values from link names, we embed both the VNETID *and* the VLAN id in
link name.  Even if this turns out not to be true, for embedding naming
purposes, we may wish to keep this as an intermediate link instead of a final
fabric link.

```
root# ip link add link sdcvxl4385813 name vx4385813v4 address 90:21:12:12:34:56 type vlan protocol 802.1Q id 4
root# ip link set vx4385813v4 up
```

### Fabric link

The final required link is the Triton Fabric link, which corresponds to a
vnic on illumos.

```
root# ip link add link vx4385813v4 name fabric0 address 90:21:12:00:00:00 type macvlan
root# ip link set fabric0 up
root# ip address add 10.21.12.12/24 dev fabric0
root# echo 1 > /proc/sys/net/ipv4/neigh/fabric0/app_solicit
```

The number space for `fabricN` will need to be managed by `net-agent`.  The
`app_solicit` parameter in procfs is a TCP/IP tunable so that RTM_GETNEIGH
messages will appear via an AF_NETLINK socket so varpd can process neighbor
(normally ARP for IPv4, and NDP for IPv6) requests. It is possible that
app_solicit should be set for the VXLAN link as well, depending.

## Netlink Interactions

The fundamental idea was to employ netlink only to trigger SVP requests.

During runtime, `varpd` reacts primarily to RTM_GETNEIGH messages,
and secondarily to RTM_DELLINK messages. I could not find a decent set of
netlink documentation save the source of iproute2, but if such documentation
exists we can probably get even better control than what we have.

### RTM_GETNEIGH

RTM_GETNEIGH messages for IPv4, IPv6, or Ethernet addresses on link-indexes
we know to be a fabric link (see [Design Choices](Design Choices) below)
trigger an SVP messages requesting a VL3 (Fabric IP) or a VL2 (Fabric MAC)
address.  We check the flags for RTM_GETNEIGH to act on either new entries
or entries performing reachability probes.

### RTM_DELLINK

RTM_DELLINK messages will cause varpd to destroy an index-to-fabric entry
(see [Design Choices](Design Choices) below).  Experiments show that if a
lower-layer link is removed, all of its upper links will be deleted, and send
RTM_DELLINK messages as well.  E.g. removing a VXLAN link will remove all of
the VLAN and Fabric links that depend on it.

### Other RTM_* messages

RTM_NEWLINK gets generated not only during link creation, but also during
other link events.  Right now we ignore it.

RTM_NEWNEIGH gets generated prior to RTM_GETNEIGH for new neighbors (ARP,
NDP, or VXLAN->underlay) and muddies the RTM_GETNEIGH waters.

RTM_* messages not mentioned are ignored for now.

## SVP Interactions

We require a `-a address` parameter for the SVP/Portolan server.  At startup
time we open a TCP connection to that address on the Portolan default port,
and send an SVP_R_PING message, and block waiting for its corresponding
SVP_R_PONG, to make sure it's working.  varpd exits if this sequence fails.

Per earlier, an RTM_GETNIGH message will cause us to send an
SVP_R_VL[23]_REQ, and we will receive an appropriate ACK.  Upon receipt of
that ACK, we will shell-out to the ip(1) command to add neighbor information.
We do this to keep netlink traffic we manage reduced, but that may change.

## Shell-out Interactions

In order to reduce netlink traffic, we shell-out to ip(1) to add neighbor
entries after receiving SVP_R_VL[23]_ACKs.  We might be able to replace this
with a dedicated neighbor-only netlink socket, but we didn't want to need to
have the RTM_GETNEIGH netlink socket have to parse through
non-trigger-packets.


## Other Design Choices

We maintain an internal array that maps interface indices to internal state
that includes the interface name, its VLAN ID, and a pointer to the VXLAN
version.  The VXLAN entry has VXLAN ID and a NULL underlying pointer.

There are still plenty of unresolved issues (look for KEBE comments), but
it now performs its fundamental task.
