cmd_/home/sj484/hw5/Module.symvers := sed 's/\.ko$$/\.o/' /home/sj484/hw5/modules.order | scripts/mod/modpost -m -a  -o /home/sj484/hw5/Module.symvers -e -i Module.symvers   -T -
