#!/usr/bin/env python3
#
# Copyright (C) 2024 Apple Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1.  Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import copy
import os
import re
import sys

_header_license = """/*
 * Copyright (C) 2024 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
"""


class SyncedData(object):
    def __init__(self, name, underlying_type_namespace, underlying_type, options):
        self.automatic_location = None
        self.conditional = None
        self.header = None

        if options is not None:
            option_list = options.split()
            for option in option_list:
                if option == 'DocumentSyncData':
                    self.automatic_location = "DocumentSyncData"
                    continue
                elif option.startswith('Conditional='):
                    self.conditional = option[12:]
                elif option.startswith('Header='):
                    self.header = option[7:]
                else:
                    raise Exception("Invalid option argument '%s' found" % option)

        self.name = name
        self.underlying_type_namespace = underlying_type_namespace
        self.underlying_type = underlying_type
        if underlying_type_namespace is None:
            self.fully_qualified_type = underlying_type
        else:
            self.fully_qualified_type = underlying_type_namespace + '::' + underlying_type


def sorted_headers_from_datas(datas):
    header_set = set()
    for data in datas:
        if data.header is None:
            continue
        header_set.add(data.header)
    return sorted(list(header_set))


def parse_process_sync_data(file):
    synched_datas = []
    headers = []
    for line in file:
        line = line.strip()

        # Skip comments
        if line.startswith('#'):
            continue

        match = re.search(r'(.*) : (.*)::(.*) \[(.*)\]', line)
        if match:
            synched_datas.append(SyncedData(match.groups()[0], match.groups()[1], match.groups()[2], match.groups()[3]))
            continue

        match = re.search(r'(.*) : (.*)::(.*)', line)
        if match:
            synched_datas.append(SyncedData(match.groups()[0], match.groups()[1], match.groups()[2], None))
            continue

        match = re.search(r'(.*) : (.*) \[(.*)\]', line)
        if match:
            synched_datas.append(SyncedData(match.groups()[0], None, match.groups()[1], match.groups()[2]))
            continue

        match = re.search(r'(.*) : (.*)', line)
        if match:
            synched_datas.append(SyncedData(match.groups()[0], None, match.groups()[1], None))
            continue

    return synched_datas


_process_sync_client_header_prefix = """
namespace WebCore {

struct ProcessSyncData;

class ProcessSyncClient {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(ProcessSyncClient);

public:
    ProcessSyncClient() = default;
    virtual ~ProcessSyncClient() = default;
"""

_process_sync_client_header_suffix = """
protected:
    virtual void broadcastProcessSyncDataToOtherProcesses(const ProcessSyncData&) { }
};

} // namespace WebCore
"""


def generate_process_sync_client_header(synched_datas):
    result = []
    result.append(_header_license)
    result.append('#pragma once\n')

    headers = sorted_headers_from_datas(synched_datas)

    headers_set = set(headers)
    headers_set.add('<wtf/TZoneMallocInlines.h>')
    headers = sorted(list(headers_set))
    for header in headers:
        result.append('#include %s' % header)

    result.append(_process_sync_client_header_prefix)

    for data in sorted(synched_datas, key=lambda data: data.name):
        if data.conditional is not None:
            result.append('#if %s' % data.conditional)
        result.append('    void broadcast%sToOtherProcesses(const %s&);' % (data.name, data.fully_qualified_type))
        if data.conditional is not None:
            result.append('#endif')

    result.append(_process_sync_client_header_suffix)
    return '\n'.join(result)


_process_sync_client_impl_prefix = """
#include "config.h"
#include "ProcessSyncClient.h"

#include "ProcessSyncData.h"

namespace WebCore {
"""


def generate_process_sync_client_impl(synched_datas):
    result = []
    result.append(_header_license)
    result.append(_process_sync_client_impl_prefix)

    for data in sorted(synched_datas, key=lambda data: data.name):
        if data.conditional is not None:
            result.append('#if %s' % data.conditional)
        result.append('void ProcessSyncClient::broadcast%sToOtherProcesses(const %s& data)' % (data.name, data.fully_qualified_type))
        result.append('{')
        result.append('    broadcastProcessSyncDataToOtherProcesses({ ProcessSyncDataType::%s, { data }});' % data.name)
        result.append('}')
        if data.conditional is not None:
            result.append('#endif')

    result.append('\n} // namespace WebCore\n')
    return '\n'.join(result)


_process_sync_data_header_suffix = """
struct ProcessSyncData {
    ProcessSyncDataType type;
    ProcessSyncDataVariant value;
};

}; // namespace WebCore
"""


def sorted_qualified_types(synched_datas):
    type_set = set()
    conditional_type_set = set()

    for data in synched_datas:
        if data.conditional is None:
            type_set.add(data)
        else:
            conditional_type_set.add(data)

    type_list = sorted(list(type_set), key=lambda data: data.fully_qualified_type)
    conditional_type_list = sorted(list(conditional_type_set), key=lambda data: data.fully_qualified_type)

    if not type_list:
        raise Exception("Surprisingly, no unconditional types found (this will make it hard to construct the variant in a way that will compile)")

    return conditional_type_list + type_list


def generate_process_sync_data_header(synched_datas):
    result = []
    result.append(_header_license)
    result.append('#pragma once\n')

    headers = sorted_headers_from_datas(synched_datas)
    headers.append('<variant>')
    for header in sorted(headers):
        result.append('#include %s' % header)

    result.append('\nnamespace WebCore {\n')
    result.append("enum class ProcessSyncDataType : uint8_t {")
    for data in sorted(synched_datas, key=lambda data: data.name):
        if data.conditional is not None:
            result.append('#if %s' % data.conditional)
        result.append('    %s,' % data.name)
        if data.conditional is not None:
            result.append('#endif')

    result.append("};")
    result.append(" ")

    sorted_variant_types = sorted_qualified_types(synched_datas)
    for data in sorted_variant_types:
        if data.conditional is not None:
            result.append('#if !%s' % data.conditional)
            result.append('using %s = bool;' % data.underlying_type)
            result.append('#endif')
    
    result.append("")
    result.append("using ProcessSyncDataVariant = std::variant<")
    for data in sorted_variant_types[:-1]:
        result.append('    %s,' % data.fully_qualified_type)

    data = sorted_variant_types[-1]
    result.append('    %s' % data.fully_qualified_type)

    result.append(">;")
    result.append(_process_sync_data_header_suffix)
    return '\n'.join(result)


_document_synced_data_header_midfix = """
namespace WebCore {

struct ProcessSyncData;

struct DocumentSyncData {
WTF_MAKE_TZONE_ALLOCATED_INLINE(DocumentSyncData);
public:
    void update(const ProcessSyncData&);
"""

_document_synched_data_header_suffix = """};

} // namespace WebCore
"""


def generate_document_synched_data_header(synched_datas):
    result = []
    result.append(_header_license)
    result.append('#pragma once\n')

    headers = []
    headers.append('<wtf/TZoneMallocInlines.h>')
    for data in synched_datas:
        if data.header is None:
            continue
        headers.append(data.header)

    for header in sorted(headers):
        result.append('#include %s' % header)

    result.append(_document_synced_data_header_midfix)

    for data in sorted(synched_datas, key=lambda data: data.name):
        if data.automatic_location != 'DocumentSyncData':
            continue
        if data.conditional is not None:
            result.append('#if %s' % data.conditional)
        name = data.name[0].lower() + data.name[1:]
        result.append('    %s %s;' % (data.fully_qualified_type, name))
        if data.conditional is not None:
            result.append('#endif')

    result.append(_document_synched_data_header_suffix)
    return '\n'.join(result)


_document_synched_data_impl_prefix = """
#include "config.h"
#include "DocumentSyncData.h"

#include "ProcessSyncData.h"

namespace WebCore {

// FIXME: Remove this once the update() function definitely always handles at least one case.
IGNORE_WARNINGS_BEGIN("missing-noreturn")
void DocumentSyncData::update(const ProcessSyncData& data)
{
    switch (data.type) {"""

_document_synched_data_impl_suffix = """    default:
        RELEASE_ASSERT_NOT_REACHED();
    }
}
IGNORE_WARNINGS_END

} //namespace WebCore
"""


def generate_document_synched_data_impl(synched_datas):
    result = []
    result.append(_header_license)
    result.append(_document_synched_data_impl_prefix)

    for data in sorted(synched_datas, key=lambda data: data.name):
        if data.automatic_location != 'DocumentSyncData':
            continue
        if data.conditional is not None:
            result.append('#if %s' % data.conditional)

        lowercase_name = data.name[0].lower() + data.name[1:]
        result.append('    case ProcessSyncDataType::%s:' % data.name)
        result.append('        %s = std::get<%s>(data.value);' % (lowercase_name, data.fully_qualified_type))
        result.append('        break;')

        if data.conditional is not None:
            result.append('#endif')

    result.append(_document_synched_data_impl_suffix)
    return '\n'.join(result)


_serialization_in_license = """#
# Copyright (C) 2024 Apple Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1.  Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
"""
_process_sync_data_serialiation_in_prefix = """
header: <WebCore/ProcessSyncData.h>
"""
_process_sync_data_serialiation_in_suffix = """
struct WebCore::ProcessSyncData {
    WebCore::ProcessSyncDataType type;
    WebCore::ProcessSyncDataVariant value;
};
"""


def generate_process_sync_data_serialiation_in(synched_datas):
    result = []
    result.append(_serialization_in_license)
    result.append(_process_sync_data_serialiation_in_prefix)

    result.append("enum class WebCore::ProcessSyncDataType : uint8_t {")
    for data in sorted(synched_datas, key=lambda data: data.name):
        if data.conditional is not None:
            result.append('#if %s' % data.conditional)
        result.append('    %s,' % data.name)
        if data.conditional is not None:
            result.append('#endif')

    result.append("};")
    result.append(" ")

    sorted_variant_types = sorted_qualified_types(synched_datas)
    for data in sorted_variant_types:
        if data.conditional is not None:
            result.append('#if !%s' % data.conditional)
            result.append('using %s = bool;' % data.fully_qualified_type)
            result.append('#endif')            
    
    result.append("")
    variant_string = "using WebCore::ProcessSyncDataVariant = std::variant<"
    for data in sorted_variant_types[:-1]:
        variant_string += data.fully_qualified_type + ', '
    variant_string += sorted_variant_types[-1].fully_qualified_type + '>;'
    result.append(variant_string)

    result.append(_process_sync_data_serialiation_in_suffix)
    return '\n'.join(result)


def main(argv):
    synched_datas = []
    headers = []

    if len(argv) < 2:
        return -1

    output_directory = ''
    if len(argv) > 2:
        output_directory = argv[2] + '/'

    with open(argv[1]) as file:
        new_synched_datas = parse_process_sync_data(file)
        for synched_data in new_synched_datas:
            synched_datas.append(synched_data)

    with open(output_directory + 'ProcessSyncClient.h', "w+") as output:
        output.write(generate_process_sync_client_header(synched_datas))

    with open(output_directory + 'ProcessSyncClient.cpp', "w+") as output:
        output.write(generate_process_sync_client_impl(synched_datas))

    with open(output_directory + 'ProcessSyncData.h', "w+") as output:
        output.write(generate_process_sync_data_header(synched_datas))

    with open(output_directory + 'ProcessSyncData.serialization.in', "w+") as output:
        output.write(generate_process_sync_data_serialiation_in(synched_datas))

    with open(output_directory + 'DocumentSyncData.h', "w+") as output:
        output.write(generate_document_synched_data_header(synched_datas))

    with open(output_directory + 'DocumentSyncData.cpp', "w+") as output:
        output.write(generate_document_synched_data_impl(synched_datas))

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
