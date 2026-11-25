
#include "minidb/sql/lexer.hpp"
#include "minidb/sql/parser.hpp"
#include "minidb/common/exception.hpp"

#include <cassert>
#include <iostream>
#include <variant>

using namespace minidb;

int main() {

    const std::string sql = R"(
        CREATE TABLE students (
            id BIGINT PRIMARY KEY,
            name VARCHAR(100) NOT NULL,
            gpa DOUBLE,
            active BOOLEAN DEFAULT TRUE
        );

        INSERT INTO students (id, name, gpa, active)
        VALUES
            (1, 'Aman', 8.5, TRUE),
            (2, 'Aniket', 7.9, FALSE);

        SELECT id, name, gpa
        FROM students
        WHERE gpa >= 8.0 AND active = TRUE
        ORDER BY id DESC
        LIMIT 10 OFFSET 0;

        UPDATE students
        SET name = 'Aman Updated', gpa = gpa + 0.5
        WHERE id = 1;

        DELETE FROM students WHERE id = 2;

        BEGIN TRANSACTION;
        COMMIT;
        ROLLBACK;

        DROP TABLE students;
    )";

    Lexer lexer(sql);

    Parser parser(lexer.Tokenize());

    const auto statements = parser.Parse();


    // ==============================================
    // TEST 1: Multiple statement parsing
    // ==============================================

    assert(statements.size() == 9);

    std::cout << "Multiple SQL statements: PASS\n";


    // ==============================================
    // TEST 2: CREATE TABLE
    // ==============================================

    const auto* create =
        std::get_if<CreateTableStatement>(
            &statements[0].data
        );

    assert(create != nullptr);

    assert(create->table_name == "students");

    assert(create->columns.size() == 4);

    assert(create->columns[0].name == "id");

    assert(create->columns[0].type == ColumnType::INT64);

    assert(create->columns[0].primary_key);

    assert(!create->columns[0].nullable);

    assert(create->columns[1].type == ColumnType::VARCHAR);

    assert(create->columns[1].max_length == 100);

    assert(!create->columns[1].nullable);

    assert(create->columns[2].type == ColumnType::DOUBLE);

    assert(create->columns[3].type == ColumnType::BOOLEAN);

    assert(create->columns[3].default_value.has_value());

    std::cout << "CREATE TABLE parsing: PASS\n";


    // ==============================================
    // TEST 3: INSERT
    // ==============================================

    const auto* insert =
        std::get_if<InsertStatement>(
            &statements[1].data
        );

    assert(insert != nullptr);

    assert(insert->table_name == "students");

    assert(insert->columns.size() == 4);

    assert(insert->rows.size() == 2);

    assert(insert->rows[0].size() == 4);

    assert(insert->rows[0][1].value == "Aman");

    assert(insert->rows[1][1].value == "Aniket");

    std::cout << "INSERT parsing: PASS\n";


    // ==============================================
    // TEST 4: SELECT
    // ==============================================

    const auto* select =
        std::get_if<SelectStatement>(
            &statements[2].data
        );

    assert(select != nullptr);

    assert(select->columns.size() == 3);

    assert(select->from_table.has_value());

    assert(*select->from_table == "students");

    assert(select->where.has_value());

    assert(select->order_by.size() == 1);

    assert(!select->order_by[0].ascending);

    assert(select->limit == 10);

    assert(select->offset == 0);

    std::cout << "SELECT parsing: PASS\n";


    // ==============================================
    // TEST 5: UPDATE
    // ==============================================

    const auto* update =
        std::get_if<UpdateStatement>(
            &statements[3].data
        );

    assert(update != nullptr);

    assert(update->table_name == "students");

    assert(update->assignments.size() == 2);

    assert(update->assignments[0].column_name == "name");

    assert(update->assignments[1].column_name == "gpa");

    assert(update->where.has_value());

    std::cout << "UPDATE parsing: PASS\n";


    // ==============================================
    // TEST 6: DELETE
    // ==============================================

    const auto* delete_statement =
        std::get_if<DeleteStatement>(
            &statements[4].data
        );

    assert(delete_statement != nullptr);

    assert(delete_statement->table_name == "students");

    assert(delete_statement->where.has_value());

    std::cout << "DELETE parsing: PASS\n";


    // ==============================================
    // TEST 7: Transaction statements
    // ==============================================

    assert(std::holds_alternative<BeginStatement>(
        statements[5].data
    ));

    assert(std::holds_alternative<CommitStatement>(
        statements[6].data
    ));

    assert(std::holds_alternative<RollbackStatement>(
        statements[7].data
    ));

    std::cout << "Transaction statement parsing: PASS\n";


    // ==============================================
    // TEST 8: DROP TABLE
    // ==============================================

    const auto* drop =
        std::get_if<DropTableStatement>(
            &statements[8].data
        );

    assert(drop != nullptr);

    assert(drop->table_name == "students");

    std::cout << "DROP TABLE parsing: PASS\n";


    // ==============================================
    // TEST 9: Operator precedence
    // AND must bind tighter than OR.
    // ==============================================

    Lexer precedence_lexer(
        "SELECT * FROM students "
        "WHERE id = 1 OR id = 2 AND active = TRUE;"
    );

    Parser precedence_parser(
        precedence_lexer.Tokenize()
    );

    const auto precedence_statements =
        precedence_parser.Parse();

    const auto& precedence_select =
        std::get<SelectStatement>(
            precedence_statements[0].data
        );

    assert(precedence_select.where.has_value());

    const Expression& root =
        *precedence_select.where;

    assert(root.kind == ExpressionKind::BINARY);

    assert(root.value == "OR");

    assert(root.children.size() == 2);

    assert(root.children[1].value == "AND");

    std::cout << "Expression precedence: PASS\n";


    // ==============================================
    // TEST 10: COUNT(*)
    // ==============================================

    Lexer count_lexer(
        "SELECT COUNT(*) AS total FROM students;"
    );

    Parser count_parser(count_lexer.Tokenize());

    const auto count_statements = count_parser.Parse();

    const auto& count_select =
        std::get<SelectStatement>(
            count_statements[0].data
        );

    assert(count_select.columns.size() == 1);

    assert(
        count_select.columns[0].expression.kind ==
        ExpressionKind::FUNCTION
    );

    assert(
        count_select.columns[0].expression.value == "COUNT"
    );

    assert(count_select.columns[0].alias == "total");

    std::cout << "COUNT(*) parsing: PASS\n";


    // ==============================================
    // TEST 11: Invalid SQL rejection
    // ==============================================

    bool invalid_sql_rejected = false;

    try {

        Lexer invalid_lexer(
            "SELECT FROM students;"
        );

        Parser invalid_parser(
            invalid_lexer.Tokenize()
        );

        invalid_parser.Parse();
    }
    catch (const MiniDBException&) {

        invalid_sql_rejected = true;
    }

    assert(invalid_sql_rejected);

    std::cout << "Invalid SQL rejection: PASS\n";


    std::cout << "\nAll SQL Parser tests passed!\n";

    return 0;
}
