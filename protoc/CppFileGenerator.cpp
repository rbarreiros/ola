// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
// http://code.google.com/p/protobuf/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Author: kenton@google.com (Kenton Varda)
//  Based on original Protocol Buffers design by
//  Sanjay Ghemawat, Jeff Dean, and others.
//
// Edited by Simon Newton for OLA

#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/printer.h>

#include <map>
#include <memory>
#include <string>

#include "protoc/CppFileGenerator.h"
#include "protoc/GeneratorHelpers.h"
#include "protoc/ServiceGenerator.h"
#include "protoc/StrUtil.h"

namespace ola {

using google::protobuf::FileDescriptor;
using google::protobuf::ServiceDescriptor;
using google::protobuf::io::Printer;
using std::auto_ptr;
using std::string;


FileGenerator::FileGenerator(const FileDescriptor *file,
                             const string &output_name)
    : m_file(file),
      m_output_name(output_name) {
  SplitStringUsing(file->package(), ".", &package_parts_);

  ServiceGenerator::Options options;
  for (int i = 0; i < file->service_count(); i++) {
    m_service_generators.push_back(
      new ServiceGenerator(file->service(i), options));
  }
}

FileGenerator::~FileGenerator() {
  ServiceGenerators::iterator iter = m_service_generators.begin();
  for (; iter != m_service_generators.end(); ++iter) {
    delete *iter;
  }
}

void FileGenerator::GenerateHeader(Printer *printer) {
  const string filename_identifier = FilenameIdentifier(m_output_name);

  std::map<string, string> var_map;
  var_map["basename"] = StripProto(m_file->name());
  var_map["filename"] = m_file->name();
  var_map["filename_identifier"] = filename_identifier;

  // Generate top of header.
  printer->Print(
    var_map,
    "// Generated by the protocol buffer compiler.  DO NOT EDIT!\n"
    "// source: $filename$\n"
    "\n"
    "#ifndef PROTOBUF_$filename_identifier$__INCLUDED  "
    "// NOLINT(build/header_guard)\n"
    "#define PROTOBUF_$filename_identifier$__INCLUDED\n"
    "\n"
    "#include <google/protobuf/service.h>\n"
    "\n"
    "#include \"$basename$.pb.h\"\n"
    "#include \"common/rpc/RpcService.h\"\n"
    "\n"
    "namespace ola {\n"
    "namespace rpc {\n"
    "class RpcController;\n"
    "class RpcChannel;\n"
    "}  // rpc\n"
    "}  // ola\n"
    "\n");

  GenerateNamespaceOpeners(printer);

  ServiceGenerators::iterator iter = m_service_generators.begin();
  for (; iter != m_service_generators.end(); ++iter) {
    (*iter)->GenerateDeclarations(printer);
  }

  GenerateNamespaceClosers(printer);

  printer->Print(
    "#endif  // PROTOBUF_$filename_identifier$__INCLUDED\n",
    "filename_identifier", filename_identifier);
}


void FileGenerator::GenerateImplementation(Printer *printer) {
  printer->Print(
    "// Generated by the protocol buffer compiler.  DO NOT EDIT!\n"
    "// source: $filename$\n"
    "\n"
    // TODO(Peter): This should be a full path to remove the lint error
    "#include \"$file$.pb.h\"\n"
    "\n"
    "#include <google/protobuf/descriptor.h>  // NOLINT(build/include)\n"
    "#include <google/protobuf/stubs/once.h>\n"
    "\n"
    "#include \"common/rpc/RpcChannel.h\"\n"
    "#include \"common/rpc/RpcController.h\"\n"
    "\n",
    "file", m_output_name,
    "filename", m_file->name());

  GenerateNamespaceOpeners(printer);

  printer->Print(
    "\n"
    "namespace {\n"
    "\n");
  for (int i = 0; i < m_file->service_count(); i++) {
    printer->Print(
      "const ::google::protobuf::ServiceDescriptor* $name$_descriptor_ =\n"
      "    NULL;\n",
      "name", m_file->service(i)->name());
  }
  printer->Print(
    "\n"
    "}  // namespace\n"
    "\n");


  // Define our externally-visible BuildDescriptors() function.  (For the lite
  // library, all this does is initialize default instances.)
  GenerateBuildDescriptors(printer);
  printer->Print("\n");
  printer->Print(kThickSeparator);
  printer->Print("\n");

  ServiceGenerators::iterator iter = m_service_generators.begin();
  for (; iter != m_service_generators.end(); ++iter) {
    (*iter)->GenerateImplementation(printer);
  }

  GenerateNamespaceClosers(printer);
}

void FileGenerator::GenerateBuildDescriptors(Printer* printer) {
  // AddDescriptors() is a file-level procedure which adds the encoded
  // FileDescriptorProto for this .proto file to the global DescriptorPool for
  // generated files (DescriptorPool::generated_pool()). It either runs at
  // static initialization time (by default) or when default_instance() is
  // called for the first time (in LITE_RUNTIME mode with
  // GOOGLE_PROTOBUF_NO_STATIC_INITIALIZER flag enabled). This procedure also
  // constructs default instances and registers extensions.
  //
  // Its sibling, AssignDescriptors(), actually pulls the compiled
  // FileDescriptor from the DescriptorPool and uses it to populate all of
  // the global variables which store pointers to the descriptor objects.
  // It also constructs the reflection objects.  It is called the first time
  // anyone calls descriptor() or GetReflection() on one of the types defined
  // in the file.

  // In optimize_for = LITE_RUNTIME mode, we don't generate AssignDescriptors()
  // and we only use AddDescriptors() to allocate default instances.
  if (HasDescriptorMethods(m_file)) {
    printer->Print(
      "\n"
      "void $assigndescriptorsname$() {\n",
      "assigndescriptorsname", GlobalAssignDescriptorsName(m_output_name));
    printer->Indent();

    // Get the file's descriptor from the pool.
    printer->Print(
      "const ::google::protobuf::FileDescriptor* file =\n"
      "  ::google::protobuf::DescriptorPool::generated_pool()->FindFileByName("
      "\n"
      "    \"$filename$\");\n"
      // Note that this GOOGLE_CHECK is necessary to prevent a warning about
      // "file" being unused when compiling an empty .proto file.
      "GOOGLE_CHECK(file != NULL);\n",
      "filename", m_file->name());

    for (int i = 0; i < m_file->service_count(); i++) {
      m_service_generators[i]->GenerateDescriptorInitializer(printer, i);
    }

    printer->Outdent();
    printer->Print(
      "}\n"
      "\n");
    // ---------------------------------------------------------------

    // protobuf_AssignDescriptorsOnce():  The first time it is called, calls
    // AssignDescriptors().  All later times, waits for the first call to
    // complete and then returns.

    // We need to generate different code, depending on the version
    // of protobuf we compile against
#if GOOGLE_PROTOBUF_VERSION < 3008000
    printer->Print(
      "namespace {\n"
      "\n"
      "GOOGLE_PROTOBUF_DECLARE_ONCE(protobuf_AssignDescriptors_once_);\n"
      "inline void protobuf_AssignDescriptorsOnce() {\n"
      "  ::google::protobuf::GoogleOnceInit(&protobuf_AssignDescriptors_once_,"
      "\n"
      "                 &$assigndescriptorsname$);\n"
      "}\n"
      "\n",
      "assigndescriptorsname", GlobalAssignDescriptorsName(m_output_name));

    printer->Print("}  // namespace\n");
#else
    printer->Print(
      "void protobuf_AssignDescriptorsOnce() {\n"
      "  static ::google::protobuf::internal::once_flag once;\n"
      "  ::google::protobuf::internal::call_once(once, $assigndescriptorsname$);\n"
      "}\n"
      "\n",
      "assigndescriptorsname", GlobalAssignDescriptorsName(m_output_name));
#endif
  }
}

void FileGenerator::GenerateNamespaceOpeners(Printer* printer) {
  if (package_parts_.size() > 0) printer->Print("\n");

  for (unsigned int i = 0; i < package_parts_.size(); i++) {
    printer->Print("namespace $part$ {\n", "part", package_parts_[i]);
  }
}

void FileGenerator::GenerateNamespaceClosers(Printer* printer) {
  if (package_parts_.size() > 0) printer->Print("\n");

  for (int i = package_parts_.size() - 1; i >= 0; i--) {
    printer->Print("}  // namespace $part$\n",
                   "part", package_parts_[i]);
  }
}
}  // namespace ola
