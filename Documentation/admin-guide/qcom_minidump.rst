Qualcomm Minidump Feature
=========================

Introduction
------------

Minidump is a best effort mechanism to collect useful and predefined
data for post-mortem debugging on a Qualcomm System on chip(SoCs).

Minidump is built on the premise that a hardware or software component
on the SoC has encountered an unexpected fault. This means that data
collected by Minidump can not be assumed to be correct or Minidump
collection itself could fail.

Qualcomm SoCs in engineering mode provides mechanism for generating
complete RAM dump for both kernel/non-kernel crashes for postmortem
debugging however, on a end user product taking complete RAM dump at
the time of failure has substantial storage requirement as well as it
is time consuming to transfer them electronically. To encounter this
problem, Minidump was introduced in Qualcomm boot firmware that provides
a way to collect selected region in the final RAM dump which is less
in size compared to complete RAM dump.

Qualcomm SoCs contains Application Processor SubSystem(APSS) and its
co-processor like Audio Digital Signal Process(ADSP), Compute DSP(CDSP),
MODEM running their operating system or firmware can register their
selected region in their respective table called SubSystem table of
content (SS-ToC) and the addresses of these tables is further maintained
in a separate table called Global Table of Content (G-ToC) inside separate
region maintaied inside RAM called Shared memory(SMEM). More about shared
memory can be found inside ``driver/soc/qcom/smem.c`` under doc section
and it is briefly discussed in later section.

It is to note that SubSystems, Remote processors and co-processors have
same meaning in this document and been used interchangeably.

High level design
-----------------
::

   +-----------------------------------------------+
   |   RAM                       +-------------+   |
   |                             |      SS0-ToC|   |
   | +----------------+     +----------------+ |   |
   | |Shared memory   |     |         SS1-ToC| |   |
   | |(SMEM)          |     |                | |   |
   | |                | +-->|--------+       | |   |
   | |G-ToC           | |   | SS-ToC  \      | |   |
   | |+-------------+ | |   | +-----------+  | |   |
   | ||-------------| | |   | |-----------|  | |   |
   | || SS0-ToC     | | | +-|<|SS1 region1|  | |   |
   | ||-------------| | | | | |-----------|  | |   |
   | || SS1-ToC     |-|>+ | | |SS1 region2|  | |   |
   | ||-------------| |   | | |-----------|  | |   |
   | || SS2-ToC     | |   | | |  ...      |  | |   |
   | ||-------------| |   | | |-----------|  | |   |
   | ||  ...        | |   |-|<|SS1 regionN|  | |   |
   | ||-------------| |   | | |-----------|  | |   |
   | || SSn-ToC     | |   | | +-----------+  | |   |
   | |+-------------+ |   | |                | |   |
   | |                |   | |----------------| |   |
   | |                |   +>|  regionN       | |   |
   | |                |   | |----------------| |   |
   | +----------------+   | |                | |   |
   |                      | |----------------| |   |
   |                      +>|  region1       | |   |
   |                        |----------------| |   |
   |                        |                | |   |
   |                        |----------------|-+   |
   |                        |  region5       |     |
   |                        |----------------|     |
   |                        |                |     |
   |  Region information    +----------------+     |
   | +---------------+                             |
   | |region name    |                             |
   | |---------------|                             |
   | |region address |                             |
   | |---------------|                             |
   | |region size    |                             |
   | +---------------+                             |
   +-----------------------------------------------+

G-ToC: Global table of contents
SSX-ToC: SubSystem X table of contents
         X is an integer in the range of 0 to 10
         Older boot firmware has kept this limit to 10
         however, in newer firmware this number is expected to change

SSX-MSn: SubSystem memory segments numbered from 0 to n
         For APSS, n is limited to 200 from older boot firmware

         Older boot firmware statically allocates 300 as total number of
         supported region across all SubSystem in Minidump table out of
         which, APSS limit is kept to 201. In future, this limitation
	 from boot firmware might get removed by allocating the region
	 dynamically. APSS Minidump kernel driver keeping this limit to
	 201 to be compatible with older boot firmware.

SMEM is a section of RAM reserved by boot firmware and is the backbone of
Minidump functionality to work. It is also a medium of inter processor
communication and a way where boot firmware can prepare something for
upcoming operating system usage.

Qualcomm SoCs boot firmware must reserve an area of RAM as SMEM prior to
handling over control to the run-time operating system. It creates SMEM
partition for Minidump with ``SBL_MINIDUMP_SMEM_ID`` and creates an array
of pointers called Global table of content (G-ToC) at the start of this
partition. Each index of this array is uniquely assigned to each SubSystem
like for APSS it is 0 while for ADSP, CDSP, MODEM it is 5, 7 and 3 respectively.
points to their table of segments called SS-ToC to be included in the Minidump.

From the diagram above, Global Table of Contents (G-ToC) enumerates a fixed
size number of SubSystem Table of Contents (SS-ToC) structures. Each
SS-ToC contains a list of SubSystem Memory Segments which are named
according to the containing SS-ToC hence, SSX-MSn where "X" denotes the
SubSystem index of the containing SSX-ToC and "n" denotes an individual
Memory segment within the SubSystem. Hence, SS0-MS0 belongs to SS0-ToC
whereas SS1-MS0 belongs to SS1-ToC. Segment structure contains name,
base address, size of a Segment to be dumped.

The Application Processor SubSystem (APSS) runs the Linux kernel and is
therefore not responsible for assembling Minidump data. One of the other
system agents in the SoC will be responsible for capturing the Minidump
data during system reset. Typically one of the SoC Digital Signal
Processors (DSP) will be used for this purpose. During reset, the DSP will
walk the G-ToC, SSX-ToCs and SSX-MSns either., dump the regions as binary
blob into storage or pushed outside to the attached host machine via USB
(more described in Dump collection section below).

Qualcomm Remote Processor Minidump support
------------------------------------------

Linux Kernel support recovery and coredump collection on remote processor
failure through remoteproc framework and in this document, remote processors
meant for ADSP, CDSP, MODEM etc. Qualcomm remoteproc driver has support for
collecting Minidump for remote processors as well where each remote processor
has their unique statically assigned descriptor in the G-ToC which is
represented via ``minidump_id`` in ``driver/remoteproc/qcom_q6v5_pas.c``
and it helps getting further information about valid registered region from
firmware and later collecting via remoteproc coredump framework.

Qualcomm APSS Minidump kernel driver concept
--------------------------------------------

Qualcomm APSS Minidump kernel driver adds the capability to add Linux
region to be dumped as part of Minidump collection. Shared memory
driver creates platform device for Minidump driver and on Minidump
driver probe it gets the G-ToC address (``struct minidump_global_toc``)
by querying Minidump SMEM ID ``SBL_MINIDUMP_SMEM_ID`` as one of parameter
to ``qcom_smem_get`` function. Further, driver uses APSS Minidump unique
descriptor or index i.e., 0 to get APSS SubSystem ToC and fills up the
fields of ``struct minidump_subsystem`` and allocates memory for Segment
array of structure ``struct minidump_region`` of size compatible with
boot firmware (default size is 201). This really means that total 201
APSS regions can be registered for APSS alone and the Minidump kernel
driver provides ``qcom_minidump_region_register`` and
``qcom_minidump_region_unregister`` function to register and unregister
APSS minidump region. Example usage explained in later section.

To simplify post-mortem debugging, APSS driver registers the first region
as an ELF header that gets updated each time a new region gets registered.
and rest 200 region can be used by other APSS Minidump driver client.

The solution supports extracting the Minidump produced either over USB
or stored to an attached storage device, if not configured default mode
is USB more described in Dump collection section.

How a kernel client driver can register region with minidump
------------------------------------------------------------

A client driver can use ``qcom_minidump_region_register`` API's to register
and ``qcom_minidump_region_unregister`` to unregister their region from
minidump driver.

A client needs to fill their region by filling ``qcom_minidump_region``
structure object which consists of the region name, region's virtual
and physical address and its size.

 .. code-block:: c

  #include <soc/qcom/qcom_minidump.h>
  [...]


  [... inside a function ...]
  struct qcom_minidump_region region;

  [...]

  client_mem_region = kzalloc(region_size, GFP_KERNEL);
  if (!client_mem_region)
	return -ENOMEM;

  [... Just write a pattern ...]
  memset(client_mem_region, 0xAB, region_size);

  [... Fill up the region object ...]
  strlcpy(region.name, "REGION_A", sizeof(region.name));
  region.virt_addr = client_mem_region;
  region.phys_addr = virt_to_phys(client_mem_region);
  region.size = region_size;

  ret = qcom_minidump_region_register(&region);
  if (ret < 0) {
	pr_err("failed to add region in minidump: err: %d\n", ret);
	return ret;
  }

  [...]


Testing
-------

Existing Qualcomm SoCs already supports collecting complete RAM dump (also
called full dump) can be configured by writing appropriate value to Qualcomm's
top control and status register (tcsr) in ``driver/firmware/qcom_scm.c``.
Complete RAM dump on system failure is where entire RAM snapshot is pushed out
to Host computer attached to SoC via USB similar to one of the way will be
used for Minidump described later in Dump collection section. Complete RAM
dump entirely get controlled from Qualcomm boot firmware and is not related
to Minidump or SMEM except the fact that same register is used to configure
one of the mode.

SCM device Tree bindings required to support download mode
For example (sm8450) ::

	/ {

	[...]

		firmware {
			scm: scm {
				compatible = "qcom,scm-sm8450", "qcom,scm";
				[... tcsr register ... ]
				qcom,dload-mode = <&tcsr 0x13000>;

				[...]
			};
		};

	[...]

		soc: soc@0 {

			[...]

			tcsr: syscon@1fc0000 {
				compatible = "qcom,sm8450-tcsr", "syscon";
				reg = <0x0 0x1fc0000 0x0 0x30000>;
			};

			[...]
		};
	[...]

	};

A kernel command line parameter is provided to facilitate selection of
dump mode also called download mode. Boot firmware configures download
mode to be full dump even before Linux boots up however, one need to pass
``qcom_scm.download_mode="mini"`` to switch the default download mode
to Minidump. Similarly ``"full"`` need to be passed to set the download
mode to full dump and passing ``"full,mini"`` will set the download mode
where both Minidump along with fulldump will be collected on system failure
however, this mode will only work if dump need to collected via USB more
about this described in Dump collection section.

Writing to sysfs node can also be used to set the mode to minidump::

	echo "mini" > /sys/module/qcom_scm/parameter/download_mode

Once the download mode is set, any kind of crash will make the device collect
respective dump as per the set download mode.

Dump collection
---------------
::

	+-----------+
	|           |
	|           |         +------+
	|           |         |      |
	|           |         +--+---+ Product(Qualcomm SoC)
	+-----------+             |
	|+++++++++++|<------------+
	|+++++++++++|    usb cable
	+-----------+
            x86_64 PC

The solution supports a product running with Qualcomm SoC (where minidump)
is supported from the firmware) connected to x86_64 host PC running PCAT
tool. It supports downloading the minidump produced from product to the
host PC over USB or to save the minidump to the product attached storage
device(UFS/eMMC/SD Card) into minidump dedicated partition.

By default, dumps are downloaded via USB to the attached x86_64 PC running
PCAT (Qualcomm tool) software. Upon download, we will see a set of binary
blobs starting with name ``md_*`` in PCAT configured directory in x86_64
machine, so for above example from the client it will be ``md_REGION_A.BIN``.
This binary blob depends on region content to determine whether it needs
external parser support to get the content of the region, so for simple
plain ASCII text we don't need any parsing and the content can be seen
just opening the binary file.

To collect the dump to attached storage type, one needs to write appropriate
value to IMEM register, in that case dumps are collected in rawdump
partition on the product device itself.

One needs to read the entire rawdump partition and pull out content to
save it onto the attached x86_64 machine over USB. Later, this rawdump
can be passed to another tool (``dexter.exe`` [Qualcomm tool]) which
converts this into the similar binary blobs which we have got it when
download type was set to USB, i.e. a set of registered regions as blobs
and their name starts with ``md_*``.

Replacing the ``dexter.exe`` with some open source tool can be added as future
scope of this document.
