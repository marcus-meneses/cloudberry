#include "association.h"
static struct acpi_device *parent(struct acpi_device *all,size_t count,uint64_t id) {
    return id && id<=count?&all[id-1]:NULL;
}
static const struct pci_record *record(const struct pci_table *pci,uint64_t index) {
    for(size_t i=0;i<pci->count;++i)if(pci->entries[i].hardware_index==index)return &pci->entries[i];
    return NULL;
}
void acpi_match_pci(struct acpi_device *all,size_t count,const struct pci_table *pci) {
    for(size_t i=0;i<count;++i){all[i].pci_hardware_index=UINT64_MAX;all[i].flags&=~(ACPI_ASSOCIATION_ERROR|ACPI_PCI_CHILD);}
    /* Namespace traversal stores parents before children. A bridge companion
     * must already have a unique match before its downstream children bind. */
    for(size_t i=0;i<count;++i) {
        struct acpi_device *dev=&all[i];
        if(!(dev->flags&ACPI_HAS_ADR) || (dev->flags&(ACPI_PCI_ROOT|ACPI_SERIAL_CHILD)))continue;
        struct acpi_device *p=parent(all,count,dev->parent_id);
        if(!p)continue;
        if((p->flags&(ACPI_PCI_ROOT|ACPI_PCI_CHILD)) || p->pci_hardware_index!=UINT64_MAX)dev->flags|=ACPI_PCI_CHILD;
        if(dev->adr>>32 || (dev->adr>>16)>31 || (dev->adr&65535)>7) {
            if(dev->flags&ACPI_PCI_CHILD)dev->flags|=ACPI_ASSOCIATION_ERROR;
            continue;
        }
        uint32_t segment,bus;
        if(p->flags&ACPI_PCI_ROOT) {
            if(p->flags&(ACPI_IDENT_ERROR|ACPI_CRS_ERROR))continue;
            segment=p->segment;bus=p->bus;
            int duplicates=0;
            for(size_t j=0;j<count;++j)if(&all[j]!=p && (all[j].flags&ACPI_PCI_ROOT) && all[j].segment==segment && all[j].bus==bus)duplicates=1;
            if(duplicates){dev->flags|=ACPI_ASSOCIATION_ERROR;continue;}
        } else {
            const struct pci_record *bridge=record(pci,p->pci_hardware_index);
            if(!bridge || (bridge->header_type&127)!=1 || !bridge->secondary_bus || (bridge->identity.flags&DEVICE_ASSOCIATION_ERROR))continue;
            segment=bridge->segment;bus=bridge->secondary_bus;
        }
        if(segment>65535 || bus>255){dev->flags|=ACPI_ASSOCIATION_ERROR;continue;}
        const struct pci_record *match=NULL;
        for(size_t j=0;j<pci->count;++j) {
            const struct pci_record *r=&pci->entries[j];
            if(r->segment==segment && r->bus==bus && r->slot==(dev->adr>>16) && r->function==(dev->adr&65535)) {
                if(match){dev->flags|=ACPI_ASSOCIATION_ERROR;match=NULL;break;}match=r;
            }
        }
        if(match)dev->pci_hardware_index=match->hardware_index;
    }
    /* Two ACPI nodes for one BDF are an ambiguity, not two device identities. */
    for(size_t i=0;i<count;++i)if(all[i].pci_hardware_index!=UINT64_MAX) {
        int duplicate=0;for(size_t j=i+1;j<count;++j)if(all[j].pci_hardware_index==all[i].pci_hardware_index){all[j].flags|=ACPI_ASSOCIATION_ERROR;duplicate=1;}
        if(duplicate)all[i].flags|=ACPI_ASSOCIATION_ERROR;
    }
    /* Descendants of an ambiguous bridge must also remain unmatched. */
    for(size_t i=0;i<count;++i) {
        struct acpi_device *p=parent(all,count,all[i].parent_id);
        if(p && (p->flags&ACPI_ASSOCIATION_ERROR))all[i].flags|=ACPI_ASSOCIATION_ERROR;
        if(all[i].flags&ACPI_ASSOCIATION_ERROR)all[i].pci_hardware_index=UINT64_MAX;
    }
}
