#include "compiler_spec_helper.h"
#include "compiler/build_tables/conflict_manager.h"

using namespace rules;
using namespace build_tables;

START_TEST

describe("resolving parse conflicts", []() {
    bool should_update;
    ConflictManager *manager;

    PreparedGrammar parse_grammar({
        { "rule1", seq({ sym("rule2"), sym("token2") }) },
        { "rule2", sym("token1") },
    }, {});

    PreparedGrammar lex_grammar({
        { "token1", pattern("[a-c]") },
        { "token2", pattern("[b-d]") },
    }, {});

    before_each([&]() {
        manager = new ConflictManager(parse_grammar, lex_grammar, {
            { Symbol("rule1"), "rule1" },
            { Symbol("rule2"), "rule2" },
            { Symbol("token1"), "token1" },
            { Symbol("token2"), "token2" },
        });
    });

    after_each([&]() {
        delete manager;
    });

    describe("lexical conflicts", [&]() {
        Symbol sym1("token1");
        Symbol sym2("token2");

        it("favors non-errors over lexical errors", [&]() {
            should_update = manager->resolve_lex_action(LexAction::Error(), LexAction::Advance(2));
            AssertThat(should_update, IsTrue());

            should_update = manager->resolve_lex_action(LexAction::Advance(2), LexAction::Error());
            AssertThat(should_update, IsFalse());
        });

        it("prefers tokens that are listed earlier in the grammar", [&]() {
            should_update = manager->resolve_lex_action(LexAction::Accept(sym1), LexAction::Accept(sym2));
            AssertThat(should_update, IsFalse());

            should_update = manager->resolve_lex_action(LexAction::Accept(sym2), LexAction::Accept(sym1));
            AssertThat(should_update, IsTrue());
        });
    });

    describe("syntactic conflicts", [&]() {
        Symbol sym1("rule1");
        Symbol sym2("rule2");

        it("favors non-errors over parse errors", [&]() {
            should_update = manager->resolve_parse_action(sym1, ParseAction::Error(), ParseAction::Shift(2, { 0 }));
            AssertThat(should_update, IsTrue());

            should_update = manager->resolve_parse_action(sym1, ParseAction::Shift(2, { 0 }), ParseAction::Error());
            AssertThat(should_update, IsFalse());
        });

        describe("shift/reduce conflicts", [&]() {
            describe("when the shift has higher precedence", [&]() {
                ParseAction shift = ParseAction::Shift(2, { 3 });
                ParseAction reduce = ParseAction::Reduce(sym2, 1, 1);
                
                it("does not record a conflict", [&]() {
                    manager->resolve_parse_action(sym1, shift, reduce);
                    manager->resolve_parse_action(sym1, reduce, shift);
                    AssertThat(manager->conflicts(), IsEmpty());
                });
                
                it("favors the shift", [&]() {
                    AssertThat(manager->resolve_parse_action(sym1, shift, reduce), IsFalse());
                    AssertThat(manager->resolve_parse_action(sym1, reduce, shift), IsTrue());
                });
            });
            
            describe("when the reduce has higher precedence", [&]() {
                ParseAction shift = ParseAction::Shift(2, { 1 });
                ParseAction reduce = ParseAction::Reduce(sym2, 1, 3);
                
                it("does not record a conflict", [&]() {
                    manager->resolve_parse_action(sym1, reduce, shift);
                    manager->resolve_parse_action(sym1, shift, reduce);
                    AssertThat(manager->conflicts(), IsEmpty());
                });
                
                it("favors the reduce", [&]() {
                    AssertThat(manager->resolve_parse_action(sym1, reduce, shift), IsFalse());
                    AssertThat(manager->resolve_parse_action(sym1, shift, reduce), IsTrue());
                });
            });
            
            describe("when the precedences are equal", [&]() {
                ParseAction shift = ParseAction::Shift(2, { 0 });
                ParseAction reduce = ParseAction::Reduce(sym2, 1, 0);

                it("records a conflict", [&]() {
                    manager->resolve_parse_action(sym1, reduce, shift);
                    manager->resolve_parse_action(sym1, shift, reduce);
                    AssertThat(manager->conflicts(), Equals(vector<Conflict>({
                        Conflict("rule1: shift (precedence 0) / reduce rule2 (precedence 0)")
                    })));
                });
                
                it("favors the shift", [&]() {
                    AssertThat(manager->resolve_parse_action(sym1, shift, reduce), IsFalse());
                    AssertThat(manager->resolve_parse_action(sym1, reduce, shift), IsTrue());
                });
            });
            
            describe("when the shift has conflicting precedences compared to the reduce", [&]() {
                ParseAction shift = ParseAction::Shift(2, { 0, 1, 3 });
                ParseAction reduce = ParseAction::Reduce(sym2, 1, 2);
                
                it("records a conflict", [&]() {
                    manager->resolve_parse_action(sym1, reduce, shift);
                    manager->resolve_parse_action(sym1, shift, reduce);
                    AssertThat(manager->conflicts(), Equals(vector<Conflict>({
                        Conflict("rule1: shift (precedence 0, 1, 3) / reduce rule2 (precedence 2)")
                    })));
                });
                
                it("favors the shift", [&]() {
                    AssertThat(manager->resolve_parse_action(sym1, shift, reduce), IsFalse());
                    AssertThat(manager->resolve_parse_action(sym1, reduce, shift), IsTrue());
                });
            });
        });

        describe("reduce/reduce conflicts", [&]() {
            describe("when one action has higher precedence", [&]() {
                ParseAction left = ParseAction::Reduce(sym2, 1, 0);
                ParseAction right = ParseAction::Reduce(sym2, 1, 3);
                
                it("favors that action", [&]() {
                    AssertThat(manager->resolve_parse_action(sym1, left, right), IsTrue());
                    AssertThat(manager->resolve_parse_action(sym1, right, left), IsFalse());
                });
                
                it("does not record a conflict", [&]() {
                    manager->resolve_parse_action(sym1, left, right);
                    manager->resolve_parse_action(sym1, right, left);
                    AssertThat(manager->conflicts(), IsEmpty());
                });
            });
            
            describe("when the actions have the same precedence", [&]() {
                ParseAction left = ParseAction::Reduce(sym1, 1, 0);
                ParseAction right = ParseAction::Reduce(sym2, 1, 0);
                
                it("favors the symbol listed earlier in the grammar", [&]() {
                    AssertThat(manager->resolve_parse_action(sym1, right, left), IsTrue());
                    AssertThat(manager->resolve_parse_action(sym1, left, right), IsFalse());
                });

                it("records a conflict", [&]() {
                    manager->resolve_parse_action(sym1, left, right);
                    manager->resolve_parse_action(sym1, right, left);
                    AssertThat(manager->conflicts(), Equals(vector<Conflict>({
                        Conflict("rule1: reduce rule2 (precedence 0) / reduce rule1 (precedence 0)"),
                        Conflict("rule1: reduce rule1 (precedence 0) / reduce rule2 (precedence 0)")
                    })));
                });
            });
        });
    });
});

END_TEST