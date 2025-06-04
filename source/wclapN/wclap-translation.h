/// NO #pragma once or include guard - this is included twice from within two different namespaces
#ifndef WCLAP_MULTIPLE_INCLUDES_NAMESPACE
#	error must not be included directly
#endif
// The matching `wclap-translation-pre.h` and `translate-clap-structs.generated.h` will already be included

namespace wclap { namespace WCLAP_MULTIPLE_INCLUDES_NAMESPACE {

struct WclapMethods;

WclapMethods * methodsCreateAndInit(Wclap &);
void methodsDeinitAndDelete(WclapMethods *);
void methodsRegister(WclapMethods *, WclapThread &);
void * methodsGetFactory(WclapMethods *, const char *);

}} // namespace
