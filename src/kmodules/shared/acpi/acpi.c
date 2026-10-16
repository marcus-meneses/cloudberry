#include "runtime.h"
#include "association.h"
#include <uacpi/utilities.h>
#include <uacpi/resources.h>
#include <uacpi/tables.h>
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static struct acpi_table table;
static struct acpi_info info;
static uacpi_namespace_node **nodes;
static size_t capacity;
static int allocation_failed;
static void *zalloc(size_t bytes) {
    unsigned char *p=uacpi_kernel_alloc(bytes);if(!p){allocation_failed=1;return NULL;}
    for(size_t i=0;i<bytes;++i)p[i]=0;
    return p;
}
static int equal(const char *x,const char *y) {
    if(!x || !y)return 0;
    while(*x && *x==*y){++x;++y;}return *x==*y;
}
static uint64_t node_id(uacpi_namespace_node *node) {
    for(size_t i=0;i<table.count;++i)if(nodes[i]==node)return i+1;
    return 0;
}
static uacpi_iteration_decision collect(void *user,uacpi_namespace_node *node,uacpi_u32 depth) {
    (void)user;(void)depth;if(uacpi_namespace_node_is_alias(node))return UACPI_ITERATION_DECISION_CONTINUE;
    if(table.count==capacity) {
        size_t next=capacity?capacity*2:16;
        if(next<capacity || next>SIZE_MAX/sizeof(struct acpi_device)){allocation_failed=1;return UACPI_ITERATION_DECISION_BREAK;}
        struct acpi_device *new_entries=zalloc(next*sizeof(*new_entries));uacpi_namespace_node **new_nodes=zalloc(next*sizeof(*new_nodes));
        if(!new_entries || !new_nodes){uacpi_kernel_free(new_entries);uacpi_kernel_free(new_nodes);return UACPI_ITERATION_DECISION_BREAK;}
        for(size_t i=0;i<table.count;++i){new_entries[i]=table.entries[i];new_nodes[i]=nodes[i];}
        uacpi_kernel_free((void *)table.entries);uacpi_kernel_free(nodes);table.entries=new_entries;nodes=new_nodes;capacity=next;
    }
    struct acpi_device *dev=(struct acpi_device *)&table.entries[table.count];
    nodes[table.count]=node;dev->reference=(struct device_acpi_ref){table.count+1,1};dev->pci_hardware_index=UINT64_MAX;
    for(uacpi_namespace_node *p=uacpi_namespace_node_parent(node);p;p=uacpi_namespace_node_parent(p))if((dev->parent_id=node_id(p)))break;
    dev->path=uacpi_namespace_node_generate_absolute_path(node);if(!dev->path)allocation_failed=1;
    ++table.count;return allocation_failed?UACPI_ITERATION_DECISION_BREAK:UACPI_ITERATION_DECISION_CONTINUE;
}
static void optional_error(struct acpi_device *dev,uacpi_status status,uint32_t flag) {
    if(status && status!=UACPI_STATUS_NOT_FOUND){dev->flags|=flag;++info.errors;}
}
static void ids(struct acpi_device *dev,uacpi_namespace_node *node) {
    uacpi_id_string *id=NULL;uacpi_status s=uacpi_eval_hid(node,&id);
    if(!s){dev->hid=acpi_copy_string(id->value);if(!dev->hid)allocation_failed=1;uacpi_free_id_string(id);}else optional_error(dev,s,ACPI_IDENT_ERROR);
    id=NULL;s=uacpi_eval_uid(node,&id);
    if(!s){dev->uid=acpi_copy_string(id->value);if(!dev->uid)allocation_failed=1;uacpi_free_id_string(id);}else optional_error(dev,s,ACPI_IDENT_ERROR);
    uacpi_pnp_id_list *list=NULL;s=uacpi_eval_cid(node,&list);
    if(!s) {
        const char **cids=zalloc((list->num_ids?list->num_ids:1)*sizeof(*cids));
        if(cids)for(unsigned i=0;i<list->num_ids;++i){cids[i]=acpi_copy_string(list->ids[i].value);if(!cids[i])allocation_failed=1;}
        dev->cids=cids;dev->cid_count=list->num_ids;uacpi_free_pnp_id_list(list);
    } else optional_error(dev,s,ACPI_IDENT_ERROR);
    if(equal(dev->hid,"PNP0A03") || equal(dev->hid,"PNP0A08"))dev->flags|=ACPI_PCI_ROOT;
    for(unsigned i=0;i<dev->cid_count && dev->cids;++i)if(equal(dev->cids[i],"PNP0A03") || equal(dev->cids[i],"PNP0A08"))dev->flags|=ACPI_PCI_ROOT;
    s=uacpi_eval_sta(node,&dev->status);optional_error(dev,s,ACPI_STA_ERROR);
    if(s)dev->status=0;
    s=uacpi_eval_adr(node,&dev->adr);if(!s)dev->flags|=ACPI_HAS_ADR;else optional_error(dev,s,ACPI_IDENT_ERROR);
    uint64_t value=0;s=uacpi_eval_simple_integer(node,"_SEG",&value);
    if(!s && value<=65535){dev->segment=value;dev->flags|=ACPI_HAS_SEG;}else optional_error(dev,!s?UACPI_STATUS_INVALID_ARGUMENT:s,ACPI_IDENT_ERROR);
    value=0;s=uacpi_eval_simple_integer(node,"_BBN",&value);
    if(!s && value<=255){dev->bus=value;dev->flags|=ACPI_HAS_BBN;}else optional_error(dev,!s?UACPI_STATUS_INVALID_ARGUMENT:s,ACPI_IDENT_ERROR);
}
struct resource_context {struct acpi_device *dev;struct device_resource *out;size_t count;unsigned index;};
static void append(struct resource_context *ctx,struct device_resource r) {
    if(ctx->out)ctx->out[ctx->count]=r;
    ++ctx->count;
}
static uint32_t irq_flags(unsigned triggering,unsigned polarity,unsigned sharing) {
    return (triggering==UACPI_TRIGGERING_LEVEL?DEVICE_RESOURCE_LEVEL:0) | (polarity==UACPI_POLARITY_ACTIVE_LOW?DEVICE_RESOURCE_LOW:0) | (sharing?DEVICE_RESOURCE_SHARED:0) | (polarity>1?DEVICE_RESOURCE_UNRESOLVED:0);
}
static uacpi_iteration_decision resource(void *user,uacpi_resource *src) {
    struct resource_context *ctx=user;struct device_resource r={.kind=DEVICE_OTHER,.flags=DEVICE_RESOURCE_UNRESOLVED,.firmware_type=src->type,.index=ctx->index++};
    switch(src->type) {
    case UACPI_RESOURCE_TYPE_END_TAG:return UACPI_ITERATION_DECISION_CONTINUE;
    case UACPI_RESOURCE_TYPE_IRQ:
        r.kind=DEVICE_IRQ;r.flags=irq_flags(src->irq.triggering,src->irq.polarity,src->irq.sharing);
        for(unsigned i=0;i<src->irq.num_irqs;++i){r.minimum=r.maximum=src->irq.irqs[i];r.length=1;append(ctx,r);}return UACPI_ITERATION_DECISION_CONTINUE;
    case UACPI_RESOURCE_TYPE_EXTENDED_IRQ:
        r.kind=DEVICE_IRQ;r.flags=irq_flags(src->extended_irq.triggering,src->extended_irq.polarity,src->extended_irq.sharing);
        if(src->extended_irq.direction==UACPI_PRODUCER)r.flags|=DEVICE_RESOURCE_PRODUCER;
        r.source=src->extended_irq.source.string;
        for(unsigned i=0;i<src->extended_irq.num_irqs;++i){r.minimum=r.maximum=src->extended_irq.irqs[i];r.length=1;append(ctx,r);}return UACPI_ITERATION_DECISION_CONTINUE;
    case UACPI_RESOURCE_TYPE_DMA:
        r.kind=DEVICE_DMA;r.flags=0;
        for(unsigned i=0;i<src->dma.num_channels;++i){r.minimum=r.maximum=src->dma.channels[i];r.length=1;append(ctx,r);}return UACPI_ITERATION_DECISION_CONTINUE;
    case UACPI_RESOURCE_TYPE_FIXED_DMA:r.kind=DEVICE_DMA;r.minimum=r.maximum=src->fixed_dma.channel;r.length=1;break;
    case UACPI_RESOURCE_TYPE_IO:r.kind=DEVICE_IO;r.flags=0;r.minimum=src->io.minimum;r.maximum=src->io.maximum;r.length=src->io.length;r.alignment=src->io.alignment;break;
    case UACPI_RESOURCE_TYPE_FIXED_IO:r.kind=DEVICE_IO;r.flags=0;r.minimum=r.maximum=src->fixed_io.address;r.length=src->fixed_io.length;break;
#define ADDRESS_CASE(tag,member) \
    case tag: {const uacpi_resource_address_common *common=&src->member.common; \
        r.kind=common->type==UACPI_RANGE_MEMORY?DEVICE_MEMORY:common->type==UACPI_RANGE_IO?DEVICE_IO:common->type==UACPI_RANGE_BUS?DEVICE_BUS_RANGE:DEVICE_OTHER; \
        r.flags=(common->direction==UACPI_PRODUCER?DEVICE_RESOURCE_PRODUCER:0) | (r.kind==DEVICE_OTHER?DEVICE_RESOURCE_UNRESOLVED:0); \
        if(common->type==UACPI_RANGE_MEMORY && !common->attribute.memory.write_status)r.flags|=DEVICE_RESOURCE_READONLY; \
        r.minimum=src->member.minimum;r.maximum=src->member.maximum;r.length=src->member.address_length;r.translation=src->member.translation_offset;r.alignment=src->member.granularity;break;}
    ADDRESS_CASE(UACPI_RESOURCE_TYPE_ADDRESS16,address16)
    ADDRESS_CASE(UACPI_RESOURCE_TYPE_ADDRESS32,address32)
    ADDRESS_CASE(UACPI_RESOURCE_TYPE_ADDRESS64,address64)
    ADDRESS_CASE(UACPI_RESOURCE_TYPE_ADDRESS64_EXTENDED,address64_extended)
#undef ADDRESS_CASE
#define MEMORY_CASE(type,member) \
    case type:r.kind=DEVICE_MEMORY;r.flags=src->member.write_status?0:DEVICE_RESOURCE_READONLY;r.minimum=src->member.minimum;r.maximum=src->member.maximum;r.length=src->member.length;r.alignment=src->member.alignment;break;
    MEMORY_CASE(UACPI_RESOURCE_TYPE_MEMORY24,memory24)
    MEMORY_CASE(UACPI_RESOURCE_TYPE_MEMORY32,memory32)
#undef MEMORY_CASE
    case UACPI_RESOURCE_TYPE_FIXED_MEMORY32:r.kind=DEVICE_MEMORY;r.flags=src->fixed_memory32.write_status?0:DEVICE_RESOURCE_READONLY;r.minimum=r.maximum=src->fixed_memory32.address;r.length=src->fixed_memory32.length;break;
    case UACPI_RESOURCE_TYPE_SERIAL_I2C_CONNECTION:case UACPI_RESOURCE_TYPE_SERIAL_SPI_CONNECTION:case UACPI_RESOURCE_TYPE_SERIAL_UART_CONNECTION:case UACPI_RESOURCE_TYPE_SERIAL_CSI2_CONNECTION:
        r.kind=DEVICE_SERIAL;r.source=src->serial_bus_common.source.string;ctx->dev->flags|=ACPI_SERIAL_CHILD;break;
    case UACPI_RESOURCE_TYPE_GPIO_CONNECTION:r.kind=DEVICE_GPIO;r.source=src->gpio_connection.source.string;break;
    default:break;
    }
    if(src->type==UACPI_RESOURCE_TYPE_ADDRESS16)r.source=src->address16.source.string;
    else if(src->type==UACPI_RESOURCE_TYPE_ADDRESS32)r.source=src->address32.source.string;
    else if(src->type==UACPI_RESOURCE_TYPE_ADDRESS64)r.source=src->address64.source.string;
    if(src->type==UACPI_RESOURCE_TYPE_MEMORY24){r.minimum<<=8;r.maximum<<=8;r.length<<=8;r.alignment=r.alignment?r.alignment:65536;}
    append(ctx,r);return UACPI_ITERATION_DECISION_CONTINUE;
}
static void resources(struct acpi_device *dev,uacpi_namespace_node *node) {
    uacpi_resources *list=NULL;uacpi_status s=uacpi_get_current_resources(node,&list);
    if(s){optional_error(dev,s,ACPI_CRS_ERROR);return;}
    struct resource_context ctx={.dev=dev};s=uacpi_for_each_resource(list,resource,&ctx);
    if(s || ctx.count>UINT32_MAX || ctx.count>SIZE_MAX/sizeof(struct device_resource)){optional_error(dev,s?s:UACPI_STATUS_OUT_OF_MEMORY,ACPI_CRS_ERROR);uacpi_free_resources(list);return;}
    if(ctx.count) {
        struct device_resource *entries=zalloc(ctx.count*sizeof(*entries));
        if(entries) {
            ctx=(struct resource_context){.dev=dev,.out=entries};uacpi_for_each_resource(list,resource,&ctx);
            for(size_t i=0;i<ctx.count;++i)if(entries[i].source){entries[i].source=acpi_copy_string(entries[i].source);if(!entries[i].source)allocation_failed=1;}
            dev->resources=entries;dev->resource_count=ctx.count;
        }
    }
    /* _BBN defaults to the root's bus resource start when firmware provides it. */
    if((dev->flags&ACPI_PCI_ROOT) && !(dev->flags&ACPI_HAS_BBN)) {
        int found=0;for(unsigned i=0;i<dev->resource_count;++i)if(dev->resources[i].kind==DEVICE_BUS_RANGE) {
            if(found || dev->resources[i].minimum>255)dev->flags|=ACPI_IDENT_ERROR;
            else dev->bus=dev->resources[i].minimum;
            found=1;
        }
    }
    uacpi_free_resources(list);
}
static void dependencies(struct acpi_device *dev,uacpi_namespace_node *node) {
    uacpi_object *object=NULL;uacpi_status s=uacpi_eval(node,"_DEP",NULL,&object);
    if(s){optional_error(dev,s,ACPI_DEP_ERROR);return;}
    uacpi_object_array array;s=uacpi_object_get_package(object,&array);
    if(!s && array.count<=UINT32_MAX && array.count<=SIZE_MAX/sizeof(uint64_t)) {
        uint64_t *out=zalloc((array.count?array.count:1)*sizeof(*out));
        if(out) {
            dev->dependencies=out;dev->dependency_count=array.count;
            for(size_t i=0;i<array.count;++i) {
                uacpi_namespace_node *target=NULL;s=uacpi_object_resolve_as_aml_namepath(array.objects[i],node,&target);
                if(s || !(out[i]=node_id(target))) {dev->flags|=ACPI_DEP_ERROR;++info.errors;}
            }
        }
    } else {dev->flags|=ACPI_DEP_ERROR;++info.errors;}
    uacpi_object_unref(object);
}
#ifndef ACPI_HOST_TEST
extern char image_start[] __attribute__((visibility("hidden")));
extern char memory_end[] __attribute__((visibility("hidden")));
extern char writable_start[] __attribute__((visibility("hidden")));
extern char acpi_relocations_start[] __attribute__((visibility("hidden")));
extern char acpi_relocations_end[] __attribute__((visibility("hidden")));
struct relocation {uint64_t offset,info;int64_t addend;};
static int relocated;
static int export_offset(const struct relocation *r,uintptr_t base) {
    const struct kmodule_image *header=(const void *)base;
    if(r->offset<header->exports_offset+32)return 0;
    uint64_t relative=r->offset-header->exports_offset-32;
    return relative%sizeof(struct kmodule_export)==0 && relative/sizeof(struct kmodule_export)<header->exports_count && *(const uint64_t *)(base+r->offset)==(uint64_t)r->addend;
}
static int relocate(void) {
    if(relocated)return 1;
    uintptr_t base=(uintptr_t)image_start,size=(uintptr_t)memory_end-base,writable=(uintptr_t)writable_start-base;
    const struct relocation *begin=(const void *)acpi_relocations_start,*end=(const void *)acpi_relocations_end;
    for(const struct relocation *r=begin;r<end;++r){
        if(r->info!=8 || r->offset>size-8 || r->addend<0 || (uint64_t)r->addend>=size)return 0;
        if(r->offset<writable && !export_offset(r,base))return 0;
    }
    /* Hub exports are image-relative offsets, not runtime pointers. The PIE
     * linker emits RELATIVE records for them too; validate but leave them RO. */
    for(const struct relocation *r=begin;r<end;++r)if(r->offset>=writable)*(uint64_t *)(base+r->offset)=base+r->addend;
    relocated=1;return 1;
}
#else
static int relocate(void) {return 1;}
#endif
static uacpi_iteration_decision check_table(uacpi_handle user,uacpi_table_info *entry) {
    int *failed=user;const char *sig=entry->signature;
    if(((sig[0]=='D' && sig[1]=='S' && sig[2]=='D' && sig[3]=='T') || (sig[0]=='S' && sig[1]=='S' && sig[2]=='D' && sig[3]=='T')) && !(entry->flags&UACPI_TABLE_LOADED))*failed=1;
    return UACPI_ITERATION_DECISION_CONTINUE;
}
struct kernel_surface_result acpi_surface_init(MODULE_ARGS) {
    MODULE_UNUSED();if(!relocate())return module_result(0,0);
    if(info.stage)return module_result(info.ready,info.ready && acpi_hub==(const void *)a && acpi_heap==b && acpi_rsdp==c);
    if(!a || !b || !c || !acpi_runtime_setup((const void *)a,b,c) || !acpi_runtime_enter())return module_result(0,0);
    info.stage=1;uacpi_status s=uacpi_initialize(UACPI_FLAG_NO_ACPI_MODE|UACPI_FLAG_BAD_CSUM_FATAL|UACPI_FLAG_BAD_TBL_SIGNATURE_FATAL);
    if(s)goto fail;
    info.stage=2;s=uacpi_namespace_load();if(s)goto fail;
    int failed=0;s=uacpi_for_each_table(check_table,&failed);if(s || failed){s=UACPI_STATUS_AML_BAD_ENCODING;goto fail;}
    info.stage=3;s=uacpi_namespace_initialize();if(s)goto fail;
    table=(struct acpi_table){.magic=ACPI_TABLE_MAGIC,.major=1,.entry_bytes=sizeof(struct acpi_device),.generation=1};
    info.stage=4;s=uacpi_namespace_for_each_child(uacpi_namespace_root(),collect,NULL,UACPI_OBJECT_DEVICE_BIT,UACPI_MAX_DEPTH_ANY,NULL);
    if(s || allocation_failed){s=UACPI_STATUS_OUT_OF_MEMORY;goto fail;}
    for(size_t i=0;i<table.count;++i) {
        struct acpi_device *dev=(struct acpi_device *)&table.entries[i];ids(dev,nodes[i]);resources(dev,nodes[i]);dependencies(dev,nodes[i]);
    }
    if(allocation_failed){s=UACPI_STATUS_OUT_OF_MEMORY;goto fail;}
    info.stage=5;
    const struct kernel_hub_table *pci=kernel_hub_find_table(acpi_hub,PCI_TABLE_NAME);
    kernel_surface_call associate=kernel_surface_find(acpi_hub,"pci.associate"),publish=kernel_surface_find(acpi_hub,"hub.publish_table");
    if(!pci || !associate || !publish){s=UACPI_STATUS_NOT_FOUND;goto fail;}
    acpi_match_pci((void *)table.entries,table.count,(const void *)pci->address);
    struct kernel_hub_table published={.name=ACPI_TABLE_NAME,.address=(uintptr_t)&table,.bytes=sizeof(table),.count=1,.entry_bytes=sizeof(table)};
    if(CALL(publish,&published,0,0).status){s=UACPI_STATUS_INTERNAL_ERROR;goto fail;}
    for(size_t i=0;i<table.count;++i) {
        struct acpi_device *dev=(struct acpi_device *)&table.entries[i];
        if(dev->pci_hardware_index!=UINT64_MAX) {
            if(CALL(associate,dev->pci_hardware_index,&dev->reference,0).status){dev->flags|=ACPI_ASSOCIATION_ERROR;dev->pci_hardware_index=UINT64_MAX;++info.unresolved;}
            else ++info.associated;
        } else if(dev->flags&ACPI_PCI_CHILD)++info.unresolved;
    }
    info.devices=table.count;info.ready=1;info.stage=6;acpi_runtime_leave();info.interpreter_errors=acpi_log_errors;return module_result(table.count,1);
fail:
    info.runtime_status=s;acpi_runtime_leave();info.interpreter_errors=acpi_log_errors;return module_result(info.stage,0);
}
static size_t resolve(const struct device_acpi_ref *ref) {
    if(!info.ready || !ref || ref->generation!=table.generation || !ref->id || ref->id>table.count)return SIZE_MAX;
    return ref->id-1;
}
struct kernel_surface_result acpi_surface_info(MODULE_ARGS) {
    MODULE_UNUSED();if(!a)return module_result(0,0);*(struct acpi_info *)a=info;return module_result(info.ready,1);
}
struct kernel_surface_result acpi_surface_device(MODULE_ARGS) {
    MODULE_UNUSED();struct device_acpi_ref ref={a,b};size_t index=resolve(&ref);
    if(index==SIZE_MAX || !c)return module_result(0,0);
    *(struct acpi_device *)c=table.entries[index];return module_result(1,1);
}
struct kernel_surface_result acpi_surface_eval_integer(MODULE_ARGS) {
    MODULE_UNUSED();size_t index=resolve((const void *)a);if(index==SIZE_MAX || !b || !c || !acpi_runtime_enter())return module_result(0,0);
    uint64_t value;uacpi_status s=uacpi_eval_simple_integer(nodes[index],(const void *)b,&value);
    if(!s)*(uint64_t *)c=value;
    acpi_runtime_leave();return module_result(s,!s);
}
struct kernel_surface_result acpi_surface_eval_buffer(MODULE_ARGS) {
    MODULE_UNUSED();size_t index=resolve((const void *)a);if(index==SIZE_MAX || !b || (!c && d) || !e || !acpi_runtime_enter())return module_result(0,0);
    uacpi_object *object=NULL;uacpi_data_view view;uacpi_status s=uacpi_eval(nodes[index],(const void *)b,NULL,&object);
    if(!s)s=uacpi_object_get_buffer(object,&view);
    if(!s) {
        *(size_t *)e=view.length;
        if(view.length>d)s=UACPI_STATUS_INVALID_ARGUMENT;
        else for(size_t i=0;i<view.length;++i)((unsigned char *)c)[i]=view.bytes[i];
    }
    if(object)uacpi_object_unref(object);
    acpi_runtime_leave();return module_result(s,!s);
}
