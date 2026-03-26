lspci -nn
dmesg | grep -Ei 'pcie|pci|link never came up|link up'
ip link
iw dev

find /lib/modules/$(uname -r) -name 'mlan.ko' -o -name 'moal.ko' -o -name 'mwifiex*.ko'
modprobe mlan 2>/dev/null || true
modprobe moal 2>/dev/null || true
modprobe mwifiex_pcie 2>/dev/null || true
dmesg | tail -n 100
ip addr add 192.168.10.1/24 dev uap0
