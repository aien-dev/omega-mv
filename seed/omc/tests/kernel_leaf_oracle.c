/* Read-only ABI comparison oracle, not a first-party driver implementation. */
#include <linux/types.h>
struct om_leaf_pair { u64 value; u64 status; };
struct om_leaf_pair om_leaf_oracle(u64 extent, u64 start, u64 count,
                                 u64 seen, u64 expect, u64 token);
struct om_leaf_pair om_leaf_oracle(u64 extent, u64 start, u64 count,
                                 u64 seen, u64 expect, u64 token)
{
    if (seen != expect)
        return (struct om_leaf_pair){0, 9};
    if (start > extent || count > extent - start)
        return (struct om_leaf_pair){0, 3};
    return (struct om_leaf_pair){token, 0};
}
