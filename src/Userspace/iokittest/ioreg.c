/*
 * Minimal ioreg: walks the IOKit registry service plane from the root and
 * prints each entry's class name and registry name, indented by depth.
 *
 * Not a port of Apple's ioreg (which prints full CF property dictionaries) -
 * PureDarwin's IOKitLib (src/Libraries/IOKit) has no property-fetch
 * wrappers yet, so this only surfaces what's needed to debug service
 * matching: the registry tree shape, and each node's class/name.
 */
#include <PDIOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>

static void
walk(io_registry_entry_t entry, int depth)
{
	io_iterator_t children;
	io_object_t child;
	io_name_t name;
	io_name_t className;
	int i;

	if (IORegistryEntryGetName(entry, name) != KERN_SUCCESS) {
		name[0] = '\0';
	}
	if (IOObjectGetClass(entry, className) != KERN_SUCCESS) {
		className[0] = '\0';
	}

	for (i = 0; i < depth; i++) {
		printf("  ");
	}
	printf("+-o %s <class %s>\n", name, className);

	if (IORegistryEntryGetChildIterator(entry, "IOService", &children) !=
	    KERN_SUCCESS) {
		return;
	}

	while ((child = IOIteratorNext(children)) != IO_OBJECT_NULL) {
		walk(child, depth + 1);
		IOObjectRelease(child);
	}
	IOObjectRelease(children);
}

int
main(void)
{
	mach_port_t masterPort;
	io_registry_entry_t root;
	kern_return_t kr;

	kr = IOMasterPort(MACH_PORT_NULL, &masterPort);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "ioreg: IOMasterPort failed: 0x%x\n", kr);
		return 1;
	}

	kr = IORegistryGetRootEntry(masterPort, &root);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr,
		    "ioreg: IORegistryGetRootEntry failed: 0x%x\n", kr);
		return 1;
	}

	walk(root, 0);
	IOObjectRelease(root);
	return 0;
}
