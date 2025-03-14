/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 Eric Gionet (gionet.c.eric@gmail.com)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file network_connect.c
 *
 * WIFI connection libary
 *
 * Copyright (c) 2024 Eric Gionet (gionet.c.eric@gmail.com)
 *
 * MIT Licensed as described in the file LICENSE
 */
#include <esp_event.h>
#include <esp_check.h>
#include <esp_log.h>
#include <esp_types.h>

#include <esp_wifi.h>
#include <esp_netif_sntp.h>
#include <esp_sntp.h>
#include <esp_tls.h>
#include <lwip/ip_addr.h>

#include <network_connect.h>

/**
 * @brief WIFI and network definitions
 */
#define WIFI_STA_SSID                           "APOLLO"        /* home internet */
#define WIFI_STA_PASSWORD                       "42F43DA525D7"  /* home internet */
#define WIFI_CONNECT_MAXIMUM_RETRY              (10)
#define INET4_IP_FORMAT_BUFFER_SIZE             (15) // (255.255.255.255)
#define SNTP_CONFIG_DEFAULT                     ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org")
#define SNTP_TIME_SYNC_TIMEOUT_MS               (2000)
#define SNTP_TIME_SYNC_MAXIMUM_RETRY            (10)
#define SNTP_TIME_FORMAT_BUFFER_SIZE            (64)

/**
 * @brief Event group definitions
 */
/* 
 * The WIFI event group allows multiple bits for each event, but we only care about 2 events:
 *
 * 0 - WIFI connected to the AP
 * 1 - WIFI connect to the AP failed after the maximum number of retries 
 */
#define WIFI_EVTGRP_CONNECTED_BIT               (BIT0)
#define WIFI_EVTGRP_DISCONNECTED_BIT            (BIT1)


static const char *TAG = "network_connect";

/* global variables */
static esp_netif_t             *s_sta_netif            = NULL;
static volatile int             s_wifi_retry_count     = 0;
static EventGroupHandle_t       s_wifi_evtgrp_hdl      = NULL;


/**
 * @brief An event handler registered to receive WIFI events.  This subroutine is called by the WIFI event loop.
 *
 * @param handler_args The user data registered to the event.
 * @param event_base Event base for the handler (i.e. WIFI and IP events).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_wifi_event_handle_t.
 */
static inline void wifi_event_handler(void* handler_args, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    ESP_LOGD(TAG, "WIFI event dispatched from event loop base=%s, event_id=%" PRIi32, event_base, event_id);

    /* handle wifi events */
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        /* init wifi event group state bits */
        xEventGroupClearBits(s_wifi_evtgrp_hdl, WIFI_EVTGRP_CONNECTED_BIT);
        xEventGroupClearBits(s_wifi_evtgrp_hdl, WIFI_EVTGRP_DISCONNECTED_BIT);
        /* initialize wifi retry counter to 0 */
        s_wifi_retry_count = 0;
        /* connect to wifi sta */
        esp_wifi_connect();
        ESP_LOGI(TAG, "Starting wifi access point connection.");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        /* init wifi event group state bits */
        xEventGroupClearBits(s_wifi_evtgrp_hdl, WIFI_EVTGRP_CONNECTED_BIT);
        xEventGroupClearBits(s_wifi_evtgrp_hdl, WIFI_EVTGRP_DISCONNECTED_BIT);
        /* wifi connection retry handler */
        if (s_wifi_retry_count <= WIFI_CONNECT_MAXIMUM_RETRY) {
            /* connect to wifi sta */
            esp_wifi_connect();
            ESP_LOGW(TAG,"Connect to wifi access point failed, attempting retry (%d/%d)", s_wifi_retry_count, WIFI_CONNECT_MAXIMUM_RETRY);
            /* increment wifi retry counter */
            ++s_wifi_retry_count;
        } else {
            /* init wifi event group state bits */
            xEventGroupSetBits(s_wifi_evtgrp_hdl, WIFI_EVTGRP_DISCONNECTED_BIT);
            xEventGroupClearBits(s_wifi_evtgrp_hdl, WIFI_EVTGRP_CONNECTED_BIT);
            ESP_LOGE(TAG,"Unable to connect to wifi access point.");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        /* init wifi event group state bits */
        xEventGroupSetBits(s_wifi_evtgrp_hdl, WIFI_EVTGRP_CONNECTED_BIT);
        xEventGroupClearBits(s_wifi_evtgrp_hdl, WIFI_EVTGRP_DISCONNECTED_BIT);
        /* reset wifi retry counter to 0 */
        s_wifi_retry_count = 0;
        /* set got ip event data */
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Connected and got ip from wifi access point: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

/**
 * @brief An event handler registered to receive SNTP events.  This subroutine is called by the SNTP event loop.
 * 
 * @param tv Time value from event handler.
 */
static inline void sntp_time_sync_event_handler(struct timeval *tv) {
    const sntp_sync_status_t status = sntp_get_sync_status();

    ESP_LOGD(TAG, "SNTP notification of a time synchronization event.");

    /* handle sntp events */
    if(status == SNTP_SYNC_STATUS_COMPLETED) {
        ESP_LOGI(TAG, "Time synchronization completed..");
    } else if (status == SNTP_SYNC_STATUS_IN_PROGRESS) {
        ESP_LOGI(TAG, "Time synchronization in progress...");
    } else if (status == SNTP_SYNC_STATUS_RESET) {
        ESP_LOGI(TAG, "Time synchronization was reset...");
    } else {
        ESP_LOGI(TAG, "Time synchronization status is unknown...");
    }
}

/**
 * @brief Synchronizes system date-time with time server(s) over SNTP once 
 * connected to an IP network.  This is a blocking function that returns
 * once the system date-time is initialized or it returns an error when 
 * the timeout period has elapsed.
 * 
 * @return esp_err_t ESP_OK on success.
 */
static inline esp_err_t sntp_synch_time(void) {
    /* set sntp configuration */
    esp_sntp_config_t config = SNTP_CONFIG_DEFAULT;
    config.start             = false;                        // start SNTP service explicitly (after connecting)
    config.sync_cb           = sntp_time_sync_event_handler; // only if we need the notification function

    /* attempt to initialize and start sntp services */
    ESP_RETURN_ON_ERROR( esp_netif_sntp_init(&config), TAG, "Unable to initialize sntp, sntp time synchronization failed" );
    ESP_RETURN_ON_ERROR( esp_netif_sntp_start(), TAG, "Unable to start sntp, sntp time synchronization failed" );

    // attempt to synchronize system time with time server(s)
    esp_err_t ret        = ESP_OK;
    int sntp_retry_count = 1;
    //
    do {
        ret = esp_netif_sntp_sync_wait(SNTP_TIME_SYNC_TIMEOUT_MS / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "Waiting for system date-time to be set... (%d/%d)", sntp_retry_count, SNTP_TIME_SYNC_MAXIMUM_RETRY);
    } while (ret == ESP_ERR_TIMEOUT && ++sntp_retry_count <= SNTP_TIME_SYNC_MAXIMUM_RETRY);

    /* clean-up - de-initialize sntp */
    esp_netif_sntp_deinit();

    /* validate ntp sync results */
    ESP_RETURN_ON_ERROR( ret, TAG, "Unable to synchronize system date-time with time server(s), sntp time synchronization failed" );

    return ESP_OK;
}

void print_system_time(void) {
    time_t now; struct tm timeinfo; static char strftime_buf[SNTP_TIME_FORMAT_BUFFER_SIZE];
    time(&now); localtime_r(&now, &timeinfo); strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in Halifax is: %s", strftime_buf);
}

void print_sntp_time_servers(void) {
    ESP_LOGI(TAG, "List of configured NTP servers:");
    for (uint8_t i = 0; i < SNTP_MAX_SERVERS; ++i) {
        if (esp_sntp_getservername(i)){
            ESP_LOGI(TAG, "->Server %d: %s", i, esp_sntp_getservername(i));
        } else {
            // we have an IPv4 address, let's print it
            static char buff[INET4_IP_FORMAT_BUFFER_SIZE];
            ip_addr_t const *ip = esp_sntp_getserver(i);
            if (ipaddr_ntoa_r(ip, buff, INET4_IP_FORMAT_BUFFER_SIZE) != NULL)
                ESP_LOGI(TAG, "->Server %d: %s", i, buff);
        }
    }
}

esp_err_t wifi_start(void) {
    esp_err_t                    ret = ESP_OK;
    wifi_init_config_t           cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    /* attempt to create default event loop and initialize netif */
    ESP_RETURN_ON_ERROR( esp_event_loop_create_default(), TAG, "Unable to create default event loop, wifi start failed" );
    ESP_RETURN_ON_ERROR( esp_netif_init(), TAG, "Unable to initialize netif, wifi start failed" );

    /* attempt to create default netif sta instance */
    s_sta_netif       = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE( s_sta_netif, ESP_ERR_ESP_NETIF_INIT_FAILED, TAG, "Unable to create default netif sta instance, wifi start failed");

    /* attempt to instantiate wifi event group handle */
    s_wifi_evtgrp_hdl = xEventGroupCreate();
    ESP_RETURN_ON_FALSE( s_wifi_evtgrp_hdl, ESP_ERR_INVALID_STATE, TAG, "Unable to create wifi event group handle, wifi start failed");
    
    /* attempt to initialize wifi */
    ESP_RETURN_ON_ERROR( esp_wifi_init(&cfg), TAG, "Unable to initialize wifi, wifi start failed" );

    /* attempt to register wifi event handlers */
    ESP_RETURN_ON_ERROR( esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id), TAG, 
                                                        "esp_event_handler_instance_register event any id, wifi start failed" );
    ESP_RETURN_ON_ERROR( esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip), TAG, 
                                                        "esp_event_handler_instance_register event sta got ip, wifi start failed" );

    /* set wifi configuration */
    wifi_config_t wifi_config = {
        .sta = {
            .ssid               = WIFI_STA_SSID,
            .password           = WIFI_STA_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    /* attempt to set wifi mode, configuration, storage and start wifi */
    ESP_RETURN_ON_ERROR( esp_wifi_set_mode(WIFI_MODE_STA), TAG, "esp_wifi_set_mode, wifi start failed" );
    ESP_RETURN_ON_ERROR( esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "esp_wifi_set_config, wifi start failed" );
    ESP_RETURN_ON_ERROR( esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "esp_wifi_set_storage, wifi start failed" );
    ESP_RETURN_ON_ERROR( esp_wifi_start(), TAG, "esp_wifi_start, wifi start failed" );

    /* Waiting until either the connection is established (WIFI_EVTGRP_CONNECTED_BIT) 
       or connection failed for the maximum number of re-tries (WIFI_EVTGRP_DISCONNECTED_BIT). 
       The bits are set by event_handler() (see above) */
    EventBits_t wifi_link_bits = xEventGroupWaitBits(s_wifi_evtgrp_hdl,
            WIFI_EVTGRP_CONNECTED_BIT | WIFI_EVTGRP_DISCONNECTED_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we 
       can test which event actually happened. */
    if (wifi_link_bits & WIFI_EVTGRP_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to wifi access point SSID: %s password: %s", WIFI_STA_SSID, WIFI_STA_PASSWORD);
        ret = ESP_OK;
    } else if (wifi_link_bits & WIFI_EVTGRP_DISCONNECTED_BIT) {
        ESP_LOGE(TAG, "Failed to connect to wifi access point SSID: %s, password: %s, wifi start failed", WIFI_STA_SSID, WIFI_STA_PASSWORD);
        ret = ESP_ERR_WIFI_NOT_CONNECT;
    } else {
        ESP_LOGE(TAG, "Unexpected event, wifi start failed");
        ret = ESP_ERR_WIFI_STATE;
    }

    return ret;
}

esp_err_t wifi_stop(void) {
    /* attempt to stop wifi services */
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR( err, TAG, "esp_wifi_stop, wifi stop failed" );
    
    /* de-initialize wifi and clear driver and event handlers */
    ESP_RETURN_ON_ERROR( esp_wifi_deinit(), TAG, "esp_wifi_deinit, wifi stop failed" );
    ESP_RETURN_ON_ERROR( esp_wifi_clear_default_wifi_driver_and_handlers(s_sta_netif), TAG, "esp_wifi_clear_default_wifi_driver_and_handlers, wifi stop failed" );

    /* clean-up */
    esp_netif_destroy(s_sta_netif);
    s_sta_netif = NULL;
    free(s_wifi_evtgrp_hdl);
    s_wifi_evtgrp_hdl = NULL;

    return ESP_OK;
}

esp_err_t sntp_start(const char* timezone) {
    time_t now; struct tm timeinfo;

    /* set current system time and time information */
    time(&now); localtime_r(&now, &timeinfo);

    // validate system time, tm_year will be (1970 - 1900).
    if (timeinfo.tm_year < (2016 - 1900)) {
        /* attempt to synchronize system date-time with time server */
        ESP_RETURN_ON_ERROR( sntp_synch_time(), TAG, "Unable to get sntp time from time server(s), get sntp time failed" );
    }

    /* set timezone or leave it in UTC when empty */
    if(timezone == NULL || strlen(timezone) == 0) {
        // attempt to set timezone to UTC
        ESP_RETURN_ON_FALSE( setenv("TZ", "GMT0", 1) == 0, ESP_ERR_INVALID_RESPONSE, TAG, "Unable to set UTC environment time-zone, get sntp time failed");
        tzset();
    } else {
        // attempt set user-defined timezone
        ESP_RETURN_ON_FALSE( setenv("TZ", timezone, 1) == 0, ESP_ERR_INVALID_RESPONSE, TAG, "Unable to set user-defined environment time-zone, get sntp time failed");
        tzset();
    }

    return ESP_OK;
}
