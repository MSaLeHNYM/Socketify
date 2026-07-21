#pragma once
/**
 * @file socketify.h
 * @brief Umbrella header: include this to get the whole framework.
 *
 * @code
 * #include <socketify/socketify.h>
 * @endcode
 */

#include "socketify/body.h"
#include "socketify/compression.h"
#include "socketify/cookies.h"
#include "socketify/cors.h"
#include "socketify/http.h"
#include "socketify/logging.h"
#include "socketify/middleware.h"
#include "socketify/ratelimit.h"
#include "socketify/request.h"
#include "socketify/response.h"
#include "socketify/router.h"
#include "socketify/server.h"
#include "socketify/sessions.h"
#include "socketify/sse.h"
#include "socketify/pulse.h"
#include "socketify/pulse_easy.h"
#include "socketify/pulse_media.h"
#include "socketify/json.h"
#include "socketify/validate.h"
#include "socketify/config.h"
#include "socketify/cache.h"
#include "socketify/http_client.h"
#include "socketify/static_files.h"
#include "socketify/tls.h"
#include "socketify/db.h"
