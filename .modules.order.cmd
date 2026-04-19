cmd_/home/sj484/hw5/modules.order := {   echo /home/sj484/hw5/sneaky_mod.ko; :; } | awk '!x[$$0]++' - > /home/sj484/hw5/modules.order
