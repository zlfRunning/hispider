#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "logger.h"
#ifdef _DEBUG_LOGGER
int main()
{
    void *logger = NULL;

    LOGGER_INIT(logger, "/tmp/test.log");
    DEBUG_LOGGER(logger, "%s", "____TEST____");
    WARN_LOGGER(logger, "%s", "____TEST____");
    ERROR_LOGGER(logger, "%s", "____TEST____");
    FATAL_LOGGER(logger, "%s", "____TEST____");
    LOGGER_CLEAN(logger);
}
#endif
