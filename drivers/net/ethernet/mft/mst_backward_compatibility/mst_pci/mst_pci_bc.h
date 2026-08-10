#ifndef MST_PCI_H
#define MST_PCI_H

/* These will be specific for PCI */
#define PCI_MAGIC 0xD1

#define PCI_INIT _IOC(_IOC_NONE,PCI_MAGIC,0,sizeof(struct mst_pci_init_st))
struct mst_pci_init_st {
	unsigned int domain;
	unsigned int bus;
	unsigned int devfn;
	int bar;
};

#define PCI_STOP _IOC(_IOC_NONE,PCI_MAGIC,1,0)

#define PCI_PARAMS    _IOR(PCI_MAGIC,2, struct mst_pci_params_st)

struct mst_pci_params_st {
	unsigned long long __attribute__((packed)) bar;
	unsigned long long __attribute__((packed)) size;
};


#define CONNECTX_WA_BASE 0xf0384 // SEM BASE ADDR. SEM 0xf0380 is reserved for external tools usage.
#define CONNECTX_WA_SIZE 3       // Size in entries

#define PCI_CONNECTX_WA _IOR(PCI_MAGIC,3, u_int32_t)


#endif
