/*
 * This file is part of Redqueen.
 *
 * Sergej Schumilo, 2019 <sergej@schumilo.de>
 * Cornelius Aschermann, 2019 <cornelius.aschermann@rub.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

HXCOMM Use DEFHEADING() to define headings in both help text and texi
HXCOMM Text between STEXI and ETEXI are copied to texi version and
HXCOMM discarded from C version
HXCOMM DEF(command, args, callback, arg_string, help) is used to construct
HXCOMM monitor info commands
HXCOMM HXCOMM can be used for comments, discarded from both texi and C

#if defined(CONFIG_PROCESSOR_TRACE)

{
    .name       = "enable",
    .args_type  = "id:i",
    .params     = "id",
    .help       = "enable processor tracing for specified vcpu",
    .cmd  = hmp_pt_enable,
},
{
    .name       = "enable_all",
    .args_type  = "",
    .params     = "",
    .help       = "enable processor tracing for all presented vcpus",
    .cmd  = hmp_pt_enable_all,
},
{
    .name       = "disable",
    .args_type  = "id:i",
    .params     = "id",
    .help       = "disable processor tracing for specified vcpu",
    .cmd  = hmp_pt_disable,
},
{
    .name       = "disable_all",
    .args_type  = "",
    .params     = "",
    .help       = "disable processor tracing for all presented vcpus",
    .cmd  = hmp_pt_disable_all,
},
{
    .name       = "status",
    .args_type  = "id:i",
    .params     = "id",
    .help       = "print processor tracing status of specified vcpu",
    .cmd  = hmp_pt_status,
},
{
    .name       = "status_all",
    .args_type  = "",
    .params     = "",
    .help       = "print processor tracing status of all presented vcpus",
    .cmd  = hmp_pt_status_all,
},
{
    .name       = "ip_filtering",
    .args_type  = "id:i,addrn:i,addr_a:l,addr_b:l",
    .params     = "id addrn (0-4) addr_a addr_b",
    .help       = "enables ip-filtering for specified vcpu",
    .cmd  = hmp_pt_ip_filtering,
},
{
    .name       = "set_file",
    .args_type  = "file:s",
    .params     = "file",
    .help       = "set output file for all specified vcpu (postfix: _cpuid)",
    .cmd  = hmp_pt_set_file,
},
        
#endif
