/****************************************************************************
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#ifdef CONFIG_GEMINI_S1_NSH

#if !(defined(CONFIG_BOARDCTL_RESET_CAUSE) && !defined(CONFIG_NSH_DISABLE_RESET_CAUSE))
#error "Deps: defined(CONFIG_BOARDCTL_RESET_CAUSE) && !defined(CONFIG_NSH_DISABLE_RESET_CAUSE)"
#endif
#if !defined(CONFIG_KVDB)
#error "Deps: defined(CONFIG_KVDB)"
#endif

set -x
echo "You're running an nsh image."

mount -t romfs /dev/res /resource
mount -t yaffs /dev/usrdata /data
set errcode $?
if [ $errcode -ne 0 ]
then
  echo "/dev/usrdata mount failed, errcode is" $errcode
fi

#ifdef CONFIG_SYSTEM_ADBD
adbd &
#endif

#ifdef CONFIG_KVDB_SERVER
kvdbd &
#endif

#ifdef CONFIG_MEDIA_SERVER
mediad &
#endif

set resetcause `resetcause`
echo $resetcause

if [ "$resetcause" == "cpu_soft_reset(restore)" ]
then
  echo "recovery reset system"
  sh /etc/factory.sh
  echo "reboot system now"
  reboot
fi

#ifndef CONFIG_LCD_DEV
#ifdef CONFIG_SHOW_LOGO
/* Do not show logo if system crashed or during silient OTA. */
if [ "$resetcause" == "cpu_soft_reset(panic)" ]
then
  echo "System crashed, hide logo"
else
  set silent_reboot_flag `getprop persist.silent_reboot.flag`

  /* Show logo firstly before reset system, if rst_btn is pressed.*/
  if [ -z $silent_reboot_flag -o $silent_reboot_flag == false ]
  then
    showlogo -n /resource/logo/logo1.bin
  fi
fi
#endif
#endif

#ifdef CONFIG_SYSTEM_NTPC
ntpcstart &
#endif

if [ -f /data/etc/wifi/wapi.conf ]
then
  sh /etc/wifi/start_wifi.sh &
fi

#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_106_HELLO_APP
home_scense &
#endif

#ifdef CONFIG_LUNCHER_MINI_APP
luncher_mini &
#endif

#ifdef CONFIG_BLUETOOTH_SERVER
sleep 8
echo "Starting bluetoothd..." 
bluetoothd &
#endif

echo "Boot nsh ok"
#endif /* CONFIG_GEMINI_S1_NSH */
