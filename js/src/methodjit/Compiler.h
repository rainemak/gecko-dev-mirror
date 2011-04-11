/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=99:
 *
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla SpiderMonkey JavaScript 1.9 code, released
 * May 28, 2008.
 *
 * The Initial Developer of the Original Code is
 *   Brendan Eich <brendan@mozilla.org>
 *
 * Contributor(s):
 *   David Anderson <danderson@mozilla.com>
 *   David Mandelin <dmandelin@mozilla.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */
#if !defined jsjaeger_compiler_h__ && defined JS_METHODJIT
#define jsjaeger_compiler_h__

#include "jsanalyze.h"
#include "jscntxt.h"
#include "jstl.h"
#include "MethodJIT.h"
#include "CodeGenIncludes.h"
#include "BaseCompiler.h"
#include "StubCompiler.h"
#include "MonoIC.h"
#include "PolyIC.h"

namespace js {
namespace mjit {

struct PatchableFrame {
    JSStackFrame *fp;
    jsbytecode *pc;
    bool scriptedCall;
};

class Compiler : public BaseCompiler
{
    friend class StubCompiler;

    struct BranchPatch {
        BranchPatch(const Jump &j, jsbytecode *pc, uint32 inlineIndex)
          : jump(j), pc(pc), inlineIndex(inlineIndex)
        { }

        Jump jump;
        jsbytecode *pc;
        uint32 inlineIndex;
    };

#if defined JS_MONOIC
    struct GlobalNameICInfo {
        Label fastPathStart;
        Call slowPathCall;
        DataLabel32 shape;
        DataLabelPtr addrLabel;
        bool usePropertyCache;

        void copyTo(ic::GlobalNameIC &to, JSC::LinkBuffer &full, JSC::LinkBuffer &stub) {
            to.fastPathStart = full.locationOf(fastPathStart);

            int offset = full.locationOf(shape) - to.fastPathStart;
            to.shapeOffset = offset;
            JS_ASSERT(to.shapeOffset == offset);

            to.slowPathCall = stub.locationOf(slowPathCall);
            to.usePropertyCache = usePropertyCache;
        }
    };

    struct GetGlobalNameICInfo : public GlobalNameICInfo {
        Label load;
    };

    struct SetGlobalNameICInfo : public GlobalNameICInfo {
        Label slowPathStart;
        Label fastPathRejoin;
        DataLabel32 store;
        Jump shapeGuardJump;
        ValueRemat vr;
        RegisterID objReg;
        RegisterID shapeReg;
        bool objConst;
    };

    struct EqualityGenInfo {
        DataLabelPtr addrLabel;
        Label stubEntry;
        Call stubCall;
        BoolStub stub;
        MaybeJump jumpToStub;
        Label fallThrough;
        jsbytecode *jumpTarget;
        bool trampoline;
        Label trampolineStart;
        ValueRemat lvr, rvr;
        Assembler::Condition cond;
        JSC::MacroAssembler::RegisterID tempReg;
    };
    
    struct TraceGenInfo {
        bool initialized;
        Label stubEntry;
        DataLabelPtr addrLabel;
        jsbytecode *jumpTarget;
        bool fastTrampoline;
        Label trampolineStart;
        Jump traceHint;
        MaybeJump slowTraceHint;

        TraceGenInfo() : initialized(false) {}
    };

    /* InlineFrameAssembler wants to see this. */
  public:
    struct CallGenInfo {
        /*
         * These members map to members in CallICInfo. See that structure for
         * more comments.
         */
        uint32       callIndex;
        DataLabelPtr funGuard;
        Jump         funJump;
        Jump         hotJump;
        Call         oolCall;
        Label        joinPoint;
        Label        slowJoinPoint;
        Label        slowPathStart;
        Label        hotPathLabel;
        DataLabelPtr addrLabel1;
        DataLabelPtr addrLabel2;
        Jump         oolJump;
        Label        icCall;
        RegisterID   funObjReg;
        RegisterID   funPtrReg;
        FrameSize    frameSize;
        bool         typeMonitored;
        types::ClonedTypeSet *argTypes;
    };

  private:
#endif

    /*
     * Writes of call return addresses which needs to be delayed until the final
     * absolute address of the join point is known.
     */
    struct CallPatchInfo {
        CallPatchInfo() : hasFastNcode(false), hasSlowNcode(false), joinSlow(false) {}
        Label joinPoint;
        DataLabelPtr fastNcodePatch;
        DataLabelPtr slowNcodePatch;
        bool hasFastNcode;
        bool hasSlowNcode;
        bool joinSlow;
    };

    struct BaseICInfo {
        BaseICInfo(JSOp op) : op(op)
        { }
        Label fastPathStart;
        Label fastPathRejoin;
        Label slowPathStart;
        Call slowPathCall;
        DataLabelPtr paramAddr;
        JSOp op;

        void copyTo(ic::BaseIC &to, JSC::LinkBuffer &full, JSC::LinkBuffer &stub) {
            to.fastPathStart = full.locationOf(fastPathStart);
            to.fastPathRejoin = full.locationOf(fastPathRejoin);
            to.slowPathStart = stub.locationOf(slowPathStart);
            to.slowPathCall = stub.locationOf(slowPathCall);
            to.op = op;
            JS_ASSERT(to.op == op);
        }
    };

    struct GetElementICInfo : public BaseICInfo {
        GetElementICInfo(JSOp op) : BaseICInfo(op)
        { }
        RegisterID  typeReg;
        RegisterID  objReg;
        ValueRemat  id;
        MaybeJump   typeGuard;
        Jump        claspGuard;
    };

    struct SetElementICInfo : public BaseICInfo {
        SetElementICInfo(JSOp op) : BaseICInfo(op)
        { }
        RegisterID  objReg;
        StateRemat  objRemat;
        ValueRemat  vr;
        Jump        capacityGuard;
        Jump        claspGuard;
        Jump        holeGuard;
        Int32Key    key;
        uint32      volatileMask;
    };

    struct PICGenInfo : public BaseICInfo {
        PICGenInfo(ic::PICInfo::Kind kind, JSOp op, bool usePropCache)
          : BaseICInfo(op), kind(kind), usePropCache(usePropCache), typeMonitored(false)
        { }
        ic::PICInfo::Kind kind;
        Label typeCheck;
        RegisterID shapeReg;
        RegisterID objReg;
        RegisterID typeReg;
        bool usePropCache;
        Label shapeGuard;
        jsbytecode *pc;
        JSAtom *atom;
        bool hasTypeCheck;
        bool typeMonitored;
        types::ClonedTypeSet *rhsTypes;
        ValueRemat vr;
#ifdef JS_HAS_IC_LABELS
        union {
            ic::GetPropLabels getPropLabels_;
            ic::SetPropLabels setPropLabels_;
            ic::BindNameLabels bindNameLabels_;
            ic::ScopeNameLabels scopeNameLabels_;
        };

        ic::GetPropLabels &getPropLabels() {
            JS_ASSERT(kind == ic::PICInfo::GET || kind == ic::PICInfo::CALL);
            return getPropLabels_;
        }
        ic::SetPropLabels &setPropLabels() {
            JS_ASSERT(kind == ic::PICInfo::SET || kind == ic::PICInfo::SETMETHOD);
            return setPropLabels_;
        }
        ic::BindNameLabels &bindNameLabels() {
            JS_ASSERT(kind == ic::PICInfo::BIND);
            return bindNameLabels_;
        }
        ic::ScopeNameLabels &scopeNameLabels() {
            JS_ASSERT(kind == ic::PICInfo::NAME || kind == ic::PICInfo::XNAME);
            return scopeNameLabels_;
        }
#else
        ic::GetPropLabels &getPropLabels() {
            JS_ASSERT(kind == ic::PICInfo::GET || kind == ic::PICInfo::CALL);
            return ic::PICInfo::getPropLabels_;
        }
        ic::SetPropLabels &setPropLabels() {
            JS_ASSERT(kind == ic::PICInfo::SET || kind == ic::PICInfo::SETMETHOD);
            return ic::PICInfo::setPropLabels_;
        }
        ic::BindNameLabels &bindNameLabels() {
            JS_ASSERT(kind == ic::PICInfo::BIND);
            return ic::PICInfo::bindNameLabels_;
        }
        ic::ScopeNameLabels &scopeNameLabels() {
            JS_ASSERT(kind == ic::PICInfo::NAME || kind == ic::PICInfo::XNAME);
            return ic::PICInfo::scopeNameLabels_;
        }
#endif

        void copySimpleMembersTo(ic::PICInfo &ic) {
            ic.kind = kind;
            ic.shapeReg = shapeReg;
            ic.objReg = objReg;
            ic.atom = atom;
            ic.usePropCache = usePropCache;
            if (ic.isSet()) {
                ic.u.vr = vr;
            } else if (ic.isGet()) {
                ic.u.get.typeReg = typeReg;
                ic.u.get.hasTypeCheck = hasTypeCheck;
            }
            ic.typeMonitored = typeMonitored;
            ic.rhsTypes = rhsTypes;
#ifdef JS_HAS_IC_LABELS
            if (ic.isGet())
                ic.setLabels(getPropLabels());
            else if (ic.isSet())
                ic.setLabels(setPropLabels());
            else if (ic.isBind())
                ic.setLabels(bindNameLabels());
            else if (ic.isScopeName())
                ic.setLabels(scopeNameLabels());
#endif
        }

    };

    struct Defs {
        Defs(uint32 ndefs)
          : ndefs(ndefs)
        { }
        uint32 ndefs;
    };

    struct InternalCallSite {
        uint32 returnOffset;
        DataLabelPtr inlinePatch;
        uint32 inlineIndex;
        jsbytecode *inlinepc;
        size_t id;
        bool ool;

        // An AutoRejoinSite needs to capture this call site.
        bool needsRejoin;

        InternalCallSite(uint32 returnOffset,
                         uint32 inlineIndex, jsbytecode *inlinepc, size_t id,
                         bool ool, bool needsRejoin)
          : returnOffset(returnOffset),
            inlineIndex(inlineIndex), inlinepc(inlinepc), id(id),
            ool(ool), needsRejoin(needsRejoin)
        { }
    };

    struct InternalRejoinSite {
        Label label;
        jsbytecode *pc;
        size_t id;

        InternalRejoinSite(Label label, jsbytecode *pc, size_t id)
            : label(label), pc(pc), id(id)
        { }
    };

    struct AutoRejoinSite {
        Compiler *cc;
        jsbytecode *pc;

        bool force;
        bool ool;
        Label oolLabel;

        // number of call/rejoin sites when this AutoRejoinSite was created.
        uint32 startSites;
        uint32 rejoinSites;

        void *stub1;
        void *stub2;
        void *stub3;

        AutoRejoinSite(Compiler *cc, void *stub1, void *stub2 = NULL, void *stub3 = NULL)
            : cc(cc), pc(cc->PC), force(false), ool(false),
              startSites(cc->callSites.length()),
              rejoinSites(cc->rejoinSites.length()),
              stub1(stub1), stub2(stub2), stub3(stub3)
        {}

        void forceGeneration()
        {
            force = true;
        }

        /*
         * Rejoin a particular slow path label in a synced state, rather than
         * the current point of the fast path when the AutoRejoinSite finishes.
         */
        void oolRejoin(Label label)
        {
            ool = true;
            oolLabel = label;
        }

        ~AutoRejoinSite()
        {
            if (cc->a != cc->outer)
                return;
#ifdef DEBUG
            JS_ASSERT(pc == cc->PC);
            cc->checkRejoinSite(startSites, rejoinSites, stub1);
            if (stub2)
                cc->checkRejoinSite(startSites, rejoinSites, stub2);
            if (stub3)
                cc->checkRejoinSite(startSites, rejoinSites, stub3);
#endif
            if (force || cc->needRejoins(pc)) {
                cc->addRejoinSite(stub1, ool, oolLabel);
                if (stub2)
                    cc->addRejoinSite(stub2, ool, oolLabel);
                if (stub3)
                    cc->addRejoinSite(stub3, ool, oolLabel);
            }
        }
    };

    struct DoublePatch {
        double d;
        DataLabelPtr label;
        bool ool;
    };

    struct JumpTable {
        DataLabelPtr label;
        size_t offsetIndex;
    };

    struct LoopEntry {
        uint32 pcOffset;
        Label label;
    };

    JSScript *outerScript;
    bool isConstructing;

    JSObject *globalObj;

    /* Existing frames on the stack whose slots may need to be updated. */
    const Vector<PatchableFrame> *patchFrames;

    bool *savedTraps;
    Assembler masm;
    FrameState frame;

    /*
     * State for the current stack frame.
     *
     * When inlining function calls, we keep track of the state of each inline
     * frame. The state of parent frames is not modified while analyzing an
     * inner frame, though registers used by those parents can be spilled in
     * the frame (reflected by the frame's active register state).
     */

    struct ActiveFrame {
        ActiveFrame *parent;
        jsbytecode *parentPC;
        JSScript *script;
        uint32 inlineIndex;
        analyze::Script analysis;
        analyze::LifetimeScript liveness;
        Label *jumpMap;
        bool hasThisType;
        JSValueType thisType;
        JSValueType *argumentTypes;
        JSValueType *localTypes;
        uint32 depth;
        Vector<UnsyncedEntry> unsyncedEntries; // :XXX: handle OOM

        /* State for managing return from inlined frames. */
        bool needReturnValue;
        bool syncReturnValue;
        bool returnValueDouble;
        bool returnSet;
        AnyRegisterID returnRegister;
        Registers returnParentRegs;
        Registers temporaryParentRegs;
        Vector<Jump, 4, CompilerAllocPolicy> *returnJumps;

        ActiveFrame(JSContext *cx);
        ~ActiveFrame();
    };
    ActiveFrame *a;
    ActiveFrame *outer;

    JSScript *script;
    jsbytecode *PC;
    bool variadicRejoin;  /* There is a variadic rejoin for PC. */

    LoopState *loop;

    /* State spanning all stack frames. */

    js::Vector<ActiveFrame*, 4, CompilerAllocPolicy> inlineFrames;
    js::Vector<BranchPatch, 64, CompilerAllocPolicy> branchPatches;
#if defined JS_MONOIC
    js::Vector<GetGlobalNameICInfo, 16, CompilerAllocPolicy> getGlobalNames;
    js::Vector<SetGlobalNameICInfo, 16, CompilerAllocPolicy> setGlobalNames;
    js::Vector<CallGenInfo, 64, CompilerAllocPolicy> callICs;
    js::Vector<EqualityGenInfo, 64, CompilerAllocPolicy> equalityICs;
    js::Vector<TraceGenInfo, 64, CompilerAllocPolicy> traceICs;
#endif
#if defined JS_POLYIC
    js::Vector<PICGenInfo, 16, CompilerAllocPolicy> pics;
    js::Vector<GetElementICInfo, 16, CompilerAllocPolicy> getElemICs;
    js::Vector<SetElementICInfo, 16, CompilerAllocPolicy> setElemICs;
#endif
    js::Vector<CallPatchInfo, 64, CompilerAllocPolicy> callPatches;
    js::Vector<InternalCallSite, 64, CompilerAllocPolicy> callSites;
    js::Vector<InternalRejoinSite, 64, CompilerAllocPolicy> rejoinSites;
    js::Vector<DoublePatch, 16, CompilerAllocPolicy> doubleList;
    js::Vector<JumpTable, 16> jumpTables;
    js::Vector<uint32, 16> jumpTableOffsets;
    js::Vector<LoopEntry, 16> loopEntries;
    StubCompiler stubcc;
    Label invokeLabel;
    Label arityLabel;
#ifdef JS_MONOIC
    Label argsCheckStub;
    Label argsCheckFallthrough;
    Jump argsCheckJump;
#endif
    bool debugMode_;
    bool addTraceHints;
    bool inlining;
    bool oomInVector;       // True if we have OOM'd appending to a vector. 
    enum { NoApplyTricks, LazyArgsObj } applyTricks;

    Compiler *thisFromCtor() { return this; }

    friend class CompilerAllocPolicy;
  public:
    // Special atom index used to indicate that the atom is 'length'. This
    // follows interpreter usage in JSOP_LENGTH.
    enum { LengthAtomIndex = uint32(-2) };

    Compiler(JSContext *cx, JSScript *outerScript, bool isConstructing,
             const Vector<PatchableFrame> *patchFrames);
    ~Compiler();

    CompileStatus compile();

    Label getLabel() { return masm.label(); }
    bool knownJump(jsbytecode *pc);
    Label labelOf(jsbytecode *target, uint32 inlineIndex);
    void addCallSite(const InternalCallSite &callSite);
    void addReturnSite();
    void inlineStubCall(void *stub, bool needsRejoin);
    bool loadOldTraps(const Vector<CallSite> &site);

    bool debugMode() { return debugMode_; }

#ifdef DEBUG
    void checkRejoinSite(uint32 nCallSites, uint32 nRejoinSites, void *stub);
#endif
    void addRejoinSite(void *stub, bool ool, Label oolLabel);

    bool needRejoins(jsbytecode *pc)
    {
        // We'll never rejoin into an inlined frame.
        if (a != outer)
            return false;

        // We need all rejoin points if we might expand an inline frame.
        if (outerScript->inlineParents)
            return true;

        // Otherwise, only add rejoin points where there are active frames on stack.
        for (unsigned i = 0; patchFrames && i < patchFrames->length(); i++) {
            if ((*patchFrames)[i].pc == pc)
                return true;
        }
        return false;
    }

    jsbytecode *outerPC() {
        if (a == outer)
            return PC;
        ActiveFrame *scan = a;
        while (scan && scan->parent != outer)
            scan = scan->parent;
        return scan->parentPC;
    }

    jsbytecode *inlinePC() { return PC; }
    uint32 inlineIndex() { return a->inlineIndex; }

    types::TypeSet *getTypeSet(uint32 slot);
    types::TypeSet *getTypeSet(const FrameEntry *fe) { return getTypeSet(frame.indexOfFe(fe)); }

    Assembler &getAssembler(bool ool) { return ool ? stubcc.masm : masm; }

  private:
    CompileStatus performCompilation(JITScript **jitp);
    CompileStatus generatePrologue();
    CompileStatus generateMethod();
    CompileStatus generateEpilogue();
    CompileStatus finishThisUp(JITScript **jitp);
    CompileStatus pushActiveFrame(JSScript *script, uint32 argc);
    void popActiveFrame();
    void generateInlinePrologue();

    /* Analysis helpers. */
    CompileStatus prepareInferenceTypes(JSScript *script, ActiveFrame *a);
    inline bool preserveLocalType(unsigned i);
    inline bool preserveArgType(unsigned i);
    void fixDoubleTypes();
    void restoreAnalysisTypes(uint32 stackDepth);
    JSValueType knownThisType();
    JSValueType knownArgumentType(uint32 arg);
    JSValueType knownLocalType(uint32 local);
    JSValueType knownPushedType(uint32 pushed);
    bool arrayPrototypeHasIndexedProperty();
    bool mayPushUndefined(uint32 pushed);
    types::TypeSet *argTypeSet(uint32 arg);
    types::TypeSet *localTypeSet(uint32 local);
    types::TypeSet *pushedTypeSet(uint32 which);
    bool monitored(jsbytecode *pc);
    bool testSingletonProperty(JSObject *obj, jsid id);
    bool testSingletonPropertyTypes(FrameEntry *top, jsid id, bool *testObject);

    /* Non-emitting helpers. */
    void pushSyncedEntry(uint32 pushed);
    uint32 fullAtomIndex(jsbytecode *pc);
    bool jumpInScript(Jump j, jsbytecode *pc);
    bool compareTwoValues(JSContext *cx, JSOp op, const Value &lhs, const Value &rhs);
    bool canUseApplyTricks();

    /* Emitting helpers. */
    bool emitStubCmpOp(BoolStub stub, AutoRejoinSite &rejoin, jsbytecode *target, JSOp fused);
    bool iter(uintN flags);
    void iterNext();
    bool iterMore();
    void iterEnd();
    MaybeJump loadDouble(FrameEntry *fe, FPRegisterID *fpReg, bool *allocated);
#ifdef JS_POLYIC
    void passICAddress(BaseICInfo *ic);
#endif
#ifdef JS_MONOIC
    void passMICAddress(GlobalNameICInfo &mic);
#endif
    bool constructThis();
    void ensureDouble(FrameEntry *fe);

    /*
     * Ensure fe is an integer, truncating from double if necessary, or jump to
     * the slow path per uses.
     */
    void ensureInteger(FrameEntry *fe, Uses uses);

    /* Convert fe from a double to integer (per ValueToECMAInt32) in place. */
    void truncateDoubleToInt32(FrameEntry *fe, Uses uses);

    /* Opcode handlers. */
    bool jumpAndTrace(Jump j, jsbytecode *target, Jump *slow = NULL, bool *trampoline = NULL);
    bool startLoop(jsbytecode *head, Jump entry, jsbytecode *entryTarget);
    bool finishLoop(jsbytecode *head);
    void jsop_bindname(JSAtom *atom, bool usePropCache);
    void jsop_setglobal(uint32 index);
    void jsop_getglobal(uint32 index);
    void jsop_getprop_slow(JSAtom *atom, bool usePropCache = true);
    void jsop_getarg(uint32 slot);
    void jsop_setarg(uint32 slot, bool popped);
    void jsop_this();
    void emitReturn(FrameEntry *fe);
    void emitFinalReturn(Assembler &masm);
    void loadReturnValue(Assembler *masm, FrameEntry *fe);
    void emitReturnValue(Assembler *masm, FrameEntry *fe);
    void emitInlineReturnValue(FrameEntry *fe);
    void dispatchCall(VoidPtrStubUInt32 stub, uint32 argc);
    void interruptCheckHelper();
    void recompileCheckHelper();
    void emitUncachedCall(uint32 argc, bool callingNew);
    void checkCallApplySpeculation(uint32 callImmArgc, uint32 speculatedArgc,
                                   FrameEntry *origCallee, FrameEntry *origThis,
                                   MaybeRegisterID origCalleeType, RegisterID origCalleeData,
                                   MaybeRegisterID origThisType, RegisterID origThisData,
                                   Jump *uncachedCallSlowRejoin, CallPatchInfo *uncachedCallPatch);
    bool inlineCallHelper(uint32 argc, bool callingNew, FrameSize &callFrameSize);
    void fixPrimitiveReturn(Assembler *masm, FrameEntry *fe);
    bool jsop_gnameinc(JSOp op, VoidStubAtom stub, uint32 index);
    CompileStatus jsop_nameinc(JSOp op, VoidStubAtom stub, uint32 index);
    CompileStatus jsop_propinc(JSOp op, VoidStubAtom stub, uint32 index);
    void jsop_eleminc(JSOp op, VoidStub);
    void jsop_getgname(uint32 index, JSValueType type);
    void jsop_getgname_slow(uint32 index);
    void jsop_callgname_epilogue();
    void jsop_setgname(JSAtom *atom, bool usePropertyCache);
    void jsop_setgname_slow(JSAtom *atom, bool usePropertyCache);
    void jsop_bindgname();
    void jsop_setelem_slow();
    void jsop_getelem_slow();
    void jsop_callelem_slow();
    void jsop_unbrand();
    bool jsop_getprop(JSAtom *atom, JSValueType type,
                      bool typeCheck = true, bool usePropCache = true);
    bool jsop_length();
    bool jsop_setprop(JSAtom *atom, bool usePropCache = true);
    void jsop_setprop_slow(JSAtom *atom, bool usePropCache = true);
    bool jsop_callprop_slow(JSAtom *atom);
    bool jsop_callprop(JSAtom *atom);
    bool jsop_callprop_obj(JSAtom *atom);
    bool jsop_callprop_str(JSAtom *atom);
    bool jsop_callprop_generic(JSAtom *atom);
    bool jsop_instanceof();
    void jsop_name(JSAtom *atom, JSValueType type);
    bool jsop_xname(JSAtom *atom);
    void enterBlock(JSObject *obj);
    void leaveBlock();
    void emitEval(uint32 argc);
    void jsop_arguments();
    bool jsop_tableswitch(jsbytecode *pc);
    void jsop_forprop(JSAtom *atom);
    void jsop_forname(JSAtom *atom);
    void jsop_forgname(JSAtom *atom);

    /* Fast arithmetic. */
    bool jsop_binary(JSOp op, VoidStub stub, JSValueType type, types::TypeSet *typeSet);
    void jsop_binary_full(FrameEntry *lhs, FrameEntry *rhs, JSOp op, VoidStub stub,
                          JSValueType type);
    void jsop_binary_full_simple(FrameEntry *fe, JSOp op, VoidStub stub,
                                 JSValueType type);
    void jsop_binary_double(FrameEntry *lhs, FrameEntry *rhs, JSOp op, VoidStub stub,
                            JSValueType type);
    void slowLoadConstantDouble(Assembler &masm, FrameEntry *fe,
                                FPRegisterID fpreg);
    void maybeJumpIfNotInt32(Assembler &masm, MaybeJump &mj, FrameEntry *fe,
                             MaybeRegisterID &mreg);
    void maybeJumpIfNotDouble(Assembler &masm, MaybeJump &mj, FrameEntry *fe,
                              MaybeRegisterID &mreg);
    bool jsop_relational(JSOp op, BoolStub stub, AutoRejoinSite &rejoin, jsbytecode *target, JSOp fused);
    bool jsop_relational_full(JSOp op, BoolStub stub, AutoRejoinSite &rejoin, jsbytecode *target, JSOp fused);
    bool jsop_relational_double(JSOp op, BoolStub stub, AutoRejoinSite &rejoin, jsbytecode *target, JSOp fused);
    bool jsop_relational_int(JSOp op, AutoRejoinSite &rejoin, jsbytecode *target, JSOp fused);

    void emitLeftDoublePath(FrameEntry *lhs, FrameEntry *rhs, FrameState::BinaryAlloc &regs,
                            MaybeJump &lhsNotDouble, MaybeJump &rhsNotNumber,
                            MaybeJump &lhsUnknownDone);
    void emitRightDoublePath(FrameEntry *lhs, FrameEntry *rhs, FrameState::BinaryAlloc &regs,
                             MaybeJump &rhsNotNumber2);
    bool tryBinaryConstantFold(JSContext *cx, FrameState &frame, JSOp op,
                               FrameEntry *lhs, FrameEntry *rhs, Value *vp);

    /* Fast opcodes. */
    void jsop_bitop(JSOp op);
    bool jsop_mod();
    void jsop_neg();
    void jsop_bitnot();
    void jsop_not();
    void jsop_typeof();
    bool booleanJumpScript(JSOp op, jsbytecode *target);
    bool jsop_ifneq(JSOp op, jsbytecode *target);
    bool jsop_andor(JSOp op, jsbytecode *target);
    bool jsop_arginc(JSOp op, uint32 slot, bool popped);
    bool jsop_localinc(JSOp op, uint32 slot, bool popped);
    bool jsop_newinit();
    void jsop_initmethod();
    void jsop_initprop();
    void jsop_initelem();
    void jsop_setelem_dense();
    bool jsop_setelem(bool popGuaranteed);
    bool jsop_getelem(bool isCall);
    void jsop_getelem_dense(bool isPacked);
    bool isCacheableBaseAndIndex(FrameEntry *obj, FrameEntry *id);
    void jsop_stricteq(JSOp op);
    bool jsop_equality(JSOp op, BoolStub stub, AutoRejoinSite &autoRejoin, jsbytecode *target, JSOp fused);
    bool jsop_equality_int_string(JSOp op, BoolStub stub, AutoRejoinSite &autoRejoin, jsbytecode *target, JSOp fused);
    void jsop_pos();

    static inline Assembler::Condition
    GetCompareCondition(JSOp op, JSOp fused)
    {
        bool ifeq = fused == JSOP_IFEQ;
        switch (op) {
          case JSOP_GT:
            return ifeq ? Assembler::LessThanOrEqual : Assembler::GreaterThan;
          case JSOP_GE:
            return ifeq ? Assembler::LessThan : Assembler::GreaterThanOrEqual;
          case JSOP_LT:
            return ifeq ? Assembler::GreaterThanOrEqual : Assembler::LessThan;
          case JSOP_LE:
            return ifeq ? Assembler::GreaterThan : Assembler::LessThanOrEqual;
          case JSOP_EQ:
            return ifeq ? Assembler::NotEqual : Assembler::Equal;
          case JSOP_NE:
            return ifeq ? Assembler::Equal : Assembler::NotEqual;
          default:
            JS_NOT_REACHED("unrecognized op");
            return Assembler::Equal;
        }
    }

    static inline Assembler::Condition
    GetStubCompareCondition(JSOp fused)
    {
        return (fused == JSOP_IFEQ) ? Assembler::Zero : Assembler::NonZero;
    }

    /* Fast builtins. */
    JSObject *pushedSingleton(unsigned pushed);
    CompileStatus callArrayBuiltin(uint32 argc, bool callingNew);
    CompileStatus inlineNativeFunction(uint32 argc, bool callingNew);
    CompileStatus inlineScriptedFunction(uint32 argc, bool callingNew);
    CompileStatus compileMathAbsInt(FrameEntry *arg);
    CompileStatus compileMathAbsDouble(FrameEntry *arg);
    CompileStatus compileMathSqrt(FrameEntry *arg);
    CompileStatus compileMathPowSimple(FrameEntry *arg1, FrameEntry *arg2);

    enum RoundingMode { Floor, Round };
    CompileStatus compileRound(FrameEntry *arg, RoundingMode mode);

    enum GetCharMode { GetChar, GetCharCode };
    CompileStatus compileGetChar(FrameEntry *thisValue, FrameEntry *arg, GetCharMode mode);

    void prepareStubCall(Uses uses);
    Call emitStubCall(void *ptr, DataLabelPtr *pinline);
};

// Given a stub call, emits the call into the inline assembly path. If
// debug mode is on, adds the appropriate instrumentation for recompilation.
#define INLINE_STUBCALL(stub)                                               \
    inlineStubCall(JS_FUNC_TO_DATA_PTR(void *, (stub)), true)

// Same as INLINE_STUBCALL, but cannot trigger recompilation.
#define INLINE_STUBCALL_NO_REJOIN(stub)                                     \
    inlineStubCall(JS_FUNC_TO_DATA_PTR(void *, (stub)), false)

// Given a stub call, emits the call into the out-of-line assembly path. If
// debug mode is on, adds the appropriate instrumentation for recompilation.
// Unlike the INLINE_STUBCALL variant, this returns the Call offset.
#define OOL_STUBCALL(stub)                                                  \
    stubcc.emitStubCall(JS_FUNC_TO_DATA_PTR(void *, (stub)), true)

// Same as OOL_STUBCALL, but specifies a slot depth.
#define OOL_STUBCALL_LOCAL_SLOTS(stub, slots)                               \
    stubcc.emitStubCall(JS_FUNC_TO_DATA_PTR(void *, (stub)), true, (slots))

// Same as OOL_STUBCALL, but cannot trigger recompilation.
#define OOL_STUBCALL_NO_REJOIN(stub)                                        \
    stubcc.emitStubCall(JS_FUNC_TO_DATA_PTR(void *, (stub)), false)

// Define rejoin sites at a PC. For every stub or scripted call emitted, there
// must be a rejoin site which captures it. These are scope based, so the
// rejoin site must be declared before the stub call and finish its scope after
// the call has been emitted. If it is emitted, the rejoin site will rejoin
// the inline code once the scope is finished.

#define REJOIN_SITE(stub)                                                   \
    AutoRejoinSite autoRejoin(this, JS_FUNC_TO_DATA_PTR(void *, (stub)))

#define REJOIN_SITE_2(stub1, stub2)                                         \
    AutoRejoinSite autoRejoin(this, JS_FUNC_TO_DATA_PTR(void *, (stub1)),   \
                              JS_FUNC_TO_DATA_PTR(void *, (stub2)))

#define REJOIN_SITE_3(stub1, stub2, stub3)                                  \
    AutoRejoinSite autoRejoin(this, JS_FUNC_TO_DATA_PTR(void *, (stub1)),   \
                              JS_FUNC_TO_DATA_PTR(void *, (stub2)),         \
                              JS_FUNC_TO_DATA_PTR(void *, (stub3)))

#define REJOIN_SITE_ANY()                                                   \
    AutoRejoinSite autoRejoin(this, (void *) RejoinSite::VARIADIC_ID)

} /* namespace js */
} /* namespace mjit */

#endif

