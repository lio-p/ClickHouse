#include <Parsers/ASTIdentifier_fwd.h>
#include <Parsers/ASTLiteral.h>
#include <Parsers/ASTSetQuery.h>

#include <Parsers/CommonParsers.h>
#include <Parsers/ParserSetQuery.h>
#include <Parsers/ExpressionElementParsers.h>

#include <Core/Names.h>
#include <IO/ReadBufferFromString.h>
#include <IO/ReadHelpers.h>
#include <Common/FieldVisitorToString.h>
#include <Common/SettingsChanges.h>
#include <Common/typeid_cast.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int BAD_ARGUMENTS;
}


class ParserLiteralOrMap : public IParserBase
{
public:
protected:
    const char * getName() const override { return "literal or map"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected) override
    {
        {
            ParserLiteral literal;
            if (literal.parse(pos, node, expected))
                return true;
        }

        ParserToken l_br(TokenType::OpeningCurlyBrace);
        ParserToken r_br(TokenType::ClosingCurlyBrace);
        ParserToken comma(TokenType::Comma);
        ParserToken colon(TokenType::Colon);
        ParserStringLiteral literal;

        if (!l_br.ignore(pos, expected))
            return false;

        Map map;

        while (!r_br.ignore(pos, expected))
        {
            if (!map.empty() && !comma.ignore(pos, expected))
                return false;

            ASTPtr key;
            ASTPtr val;

            if (!literal.parse(pos, key, expected))
                return false;

            if (!colon.ignore(pos, expected))
                return false;

            if (!literal.parse(pos, val, expected))
                return false;

            Tuple tuple;
            tuple.push_back(std::move(key->as<ASTLiteral>()->value));
            tuple.push_back(std::move(val->as<ASTLiteral>()->value));
            map.push_back(std::move(tuple));
        }

        node = std::make_shared<ASTLiteral>(std::move(map));
        return true;
    }
};

/// Parse Literal, Array/Tuple/Map of literals, Identifier
bool parseParameterValueIntoString(IParser::Pos & pos, String & value, Expected & expected)
{
    ASTPtr node;

    /// Identifier

    ParserCompoundIdentifier identifier_p;

    if (identifier_p.parse(pos, node, expected))
    {
        tryGetIdentifierNameInto(node, value);
        return true;
    }

    /// Literal, Array/Tuple of literals

    ParserLiteral literal_p;
    ParserArrayOfLiterals array_p;
    ParserTupleOfLiterals tuple_p;

    if (literal_p.parse(pos, node, expected) ||
        array_p.parse(pos, node, expected) ||
        tuple_p.parse(pos, node, expected))
    {

        value = applyVisitor(FieldVisitorToString(), node->as<ASTLiteral>()->value);

        /// writeQuoted is not always quoted in line with SQL standard https://github.com/ClickHouse/ClickHouse/blob/master/src/IO/WriteHelpers.h
        if (value.starts_with('\''))
        {
            ReadBufferFromOwnString buf(value);
            readQuoted(value, buf);
        }

        return true;
    }

    /// Map of literals

    ParserToken l_br_p(TokenType::OpeningCurlyBrace);
    ParserToken r_br_p(TokenType::ClosingCurlyBrace);
    ParserToken comma_p(TokenType::Comma);
    ParserToken colon_p(TokenType::Colon);

    if (!l_br_p.ignore(pos, expected))
        return false;

    int depth = 1;

    value = '{';

    while (depth > 0)
    {
        if (r_br_p.ignore(pos, expected))
        {
            value += '}';
            --depth;
            continue;
        }

        if (value.back() != '{')
        {
            if (!comma_p.ignore(pos, expected))
                return false;

            value += ',';
        }

        ASTPtr key;
        ASTPtr val;

        if (!literal_p.parse(pos, key, expected))
            return false;

        if (!colon_p.ignore(pos, expected))
            return false;

        value += applyVisitor(FieldVisitorToString(), key->as<ASTLiteral>()->value);
        value += ":";

        if (l_br_p.ignore(pos, expected))
        {
            value += '{';
            ++depth;
            continue;
        }

        if (!literal_p.parse(pos, val, expected)
            && !array_p.parse(pos, val, expected)
            && !tuple_p.parse(pos, val, expected))
            return false;

        value += applyVisitor(FieldVisitorToString(), val->as<ASTLiteral>()->value);
    }

    return true;
}

/// Parse `name = value`.
bool ParserSetQuery::parseNameValuePair(SettingChange & change, IParser::Pos & pos, Expected & expected)
{
    ParserCompoundIdentifier name_p;
    ParserLiteralOrMap value_p;
    ParserToken s_eq(TokenType::Equals);

    ASTPtr name;
    ASTPtr value;

    if (!name_p.parse(pos, name, expected))
        return false;

    if (!s_eq.ignore(pos, expected))
        return false;

    if (ParserKeyword("TRUE").ignore(pos, expected))
        value = std::make_shared<ASTLiteral>(Field(static_cast<UInt64>(1)));
    else if (ParserKeyword("FALSE").ignore(pos, expected))
        value = std::make_shared<ASTLiteral>(Field(static_cast<UInt64>(0)));
    else if (!value_p.parse(pos, value, expected))
        return false;

    tryGetIdentifierNameInto(name, change.name);
    change.value = value->as<ASTLiteral &>().value;

    return true;
}

bool ParserSetQuery::parseNameValuePairWithParameterOrDefault(
    SettingChange & change, String & default_settings, ParserSetQuery::Parameter & parameter, IParser::Pos & pos, Expected & expected)
{
    ParserCompoundIdentifier name_p;
    ParserLiteralOrMap value_p;
    ParserToken s_eq(TokenType::Equals);

    ASTPtr node;
    String name;

    if (!name_p.parse(pos, node, expected))
        return false;

    if (!s_eq.ignore(pos, expected))
        return false;

    tryGetIdentifierNameInto(node, name);

    /// Parameter
    if (name.starts_with(QUERY_PARAMETER_NAME_PREFIX))
    {
        name = name.substr(strlen(QUERY_PARAMETER_NAME_PREFIX));

        if (name.empty())
            throw Exception(ErrorCodes::BAD_ARGUMENTS, "Parameter name cannot be empty");

        String value;

        if (!parseParameterValueIntoString(pos, value, expected))
            return false;

        parameter = {std::move(name), std::move(value)};
        return true;
    }

    /// Default
    if (ParserKeyword("DEFAULT").ignore(pos, expected))
    {
        default_settings = name;
        return true;
    }

    /// Setting
    if (ParserKeyword("TRUE").ignore(pos, expected))
        node = std::make_shared<ASTLiteral>(Field(static_cast<UInt64>(1)));
    else if (ParserKeyword("FALSE").ignore(pos, expected))
        node = std::make_shared<ASTLiteral>(Field(static_cast<UInt64>(0)));
    else if (!value_p.parse(pos, node, expected))
        return false;

    change.name = name;
    change.value = node->as<ASTLiteral &>().value;

    return true;
}


bool ParserSetQuery::parseImpl(Pos & pos, ASTPtr & node, Expected & expected)
{
    ParserToken s_comma(TokenType::Comma);

    if (!parse_only_internals)
    {
        ParserKeyword s_set("SET");

        if (!s_set.ignore(pos, expected))
            return false;

        /// Parse SET TRANSACTION ... queries using ParserTransactionControl
        if (ParserKeyword{"TRANSACTION"}.check(pos, expected))
            return false;
    }

    SettingsChanges changes;
    NameToNameMap query_parameters;
    std::vector<String> default_settings;

    while (true)
    {
        if ((!changes.empty() || !query_parameters.empty() || !default_settings.empty()) && !s_comma.ignore(pos))
            break;

        SettingChange setting;
        String name_of_default_setting;
        Parameter parameter;

        if (!parseNameValuePairWithParameterOrDefault(setting, name_of_default_setting, parameter, pos, expected))
            return false;

        if (!parameter.first.empty())
            query_parameters.emplace(std::move(parameter));
        else if (!name_of_default_setting.empty())
            default_settings.emplace_back(std::move(name_of_default_setting));
        else
            changes.push_back(std::move(setting));
    }

    auto query = std::make_shared<ASTSetQuery>();
    node = query;

    query->is_standalone = !parse_only_internals;
    query->changes = std::move(changes);
    query->query_parameters = std::move(query_parameters);
    query->default_settings = std::move(default_settings);

    return true;
}


}
