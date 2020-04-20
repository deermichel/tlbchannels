// memory hierarchy architecture of various processors

#ifndef ARCH_H
#define ARCH_H

// broadwell stlb: 1536 entries, 6-way, 256 sets
// 48 bits va    |20 bits   |16 bits tlb set (XOR-8)   |12 bits page offset (4k pages)
//            0x 00000      00 00                      000
#if defined(ARCH_BROADWELL)
    #define TLB_ENTRIES     1536
    #define TLB_SETS        256
    #define TLB_WAYS        6
    #define VA_SET_OFFSET   12      // 4k pages
    #define VA_WAY_OFFSET   28      // skip XOR-8 set selection

// skylake stlb: 1536 entries, 12-way, 128 sets
// 48 bits va    |22 bits   |14 bits tlb set (XOR-7)   |12 bits page offset (4k pages)
#elif defined(ARCH_SKYLAKE)
    #define TLB_ENTRIES     1536
    #define TLB_SETS        128
    #define TLB_WAYS        12
    #define VA_SET_OFFSET   12      // 4k pages
    #define VA_WAY_OFFSET   26      // skip XOR-7 set selection

#else
    #error Please specify a CPU architecture.
#endif

#endif // ARCH_H