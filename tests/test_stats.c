#include "../include/orangepool.h"
#include "test_common.h"

static void *task(void *arg) {
    (void)arg;
    return NULL;
}

int main(void) {
    orangepool *pool = NULL;
    orangepool_stats s;

    ASSERT_EQ_INT(orangepool_create(&pool, 2, 8), ORANGEPOOL_OK);
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ_INT(orangepool_submit(pool, task, NULL, NULL), ORANGEPOOL_OK);
    }
    ASSERT_EQ_INT(orangepool_shutdown(pool, ORANGEPOOL_SHUTDOWN_DRAIN), ORANGEPOOL_OK);

    orangepool_get_stats(pool, &s);
    ASSERT_TRUE(s.total_submitted == 5);
    ASSERT_TRUE(s.total_executed == 5);

    orangepool_destroy(pool);
    return 0;
}
