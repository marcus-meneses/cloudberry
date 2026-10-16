#ifndef CLOUDBERRY_ACPI_ASSOCIATION_H
#define CLOUDBERRY_ACPI_ASSOCIATION_H
#include "../acpi.h"
#include "../pci.h"
/* Compute all matches before publishing any. Ambiguous roots/BDFs never bind. */
void acpi_match_pci(struct acpi_device *,size_t,const struct pci_table *);
#endif
