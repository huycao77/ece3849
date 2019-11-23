# invoke SourceDir generated makefile for rtos.pem4f
rtos.pem4f: .libraries,rtos.pem4f
.libraries,rtos.pem4f: package/cfg/rtos_pem4f.xdl
	$(MAKE) -f D:\CCS_Workspace\ece3849_lab2_hqcao_vtle2/src/makefile.libs

clean::
	$(MAKE) -f D:\CCS_Workspace\ece3849_lab2_hqcao_vtle2/src/makefile.libs clean

