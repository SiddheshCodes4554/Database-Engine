#include "ast.hpp"
#include <sstream>

std::string CreateTableStatement::ToString() const {
    std::ostringstream ss;
    ss << "CREATE TABLE " << table_name_ << " (";
    for (size_t i = 0; i < columns_.size(); ++i) {
        ss << columns_[i].name << " " << columns_[i].type;
        if (i + 1 < columns_.size()) ss << ", ";
    }
    ss << ")";
    return ss.str();
}

std::string InsertStatement::ToString() const {
    std::ostringstream ss;
    ss << "INSERT INTO " << table_name_;
    if (!columns_.empty()) {
        ss << " (";
        for (size_t i = 0; i < columns_.size(); ++i) {
            ss << columns_[i];
            if (i + 1 < columns_.size()) ss << ", ";
        }
        ss << ")";
    }
    ss << " VALUES (";
    for (size_t i = 0; i < values_.size(); ++i) {
        ss << values_[i]->ToString();
        if (i + 1 < values_.size()) ss << ", ";
    }
    ss << ")";
    return ss.str();
}

std::string SelectStatement::ToString() const {
    std::ostringstream ss;
    ss << "SELECT ";
    if (columns_.empty()) {
        ss << "*";
    } else {
        for (size_t i = 0; i < columns_.size(); ++i) {
            ss << columns_[i];
            if (i + 1 < columns_.size()) ss << ", ";
        }
    }
    ss << " FROM " << table_name_;
    if (where_clause_ != nullptr) {
        ss << " WHERE " << where_clause_->ToString();
    }
    return ss.str();
}

std::string UpdateStatement::ToString() const {
    std::ostringstream ss;
    ss << "UPDATE " << table_name_ << " SET ";
    for (size_t i = 0; i < assignments_.size(); ++i) {
        ss << assignments_[i].column << " = " << assignments_[i].expression->ToString();
        if (i + 1 < assignments_.size()) ss << ", ";
    }
    if (where_clause_ != nullptr) {
        ss << " WHERE " << where_clause_->ToString();
    }
    return ss.str();
}

std::string DeleteStatement::ToString() const {
    std::ostringstream ss;
    ss << "DELETE FROM " << table_name_;
    if (where_clause_ != nullptr) {
        ss << " WHERE " << where_clause_->ToString();
    }
    return ss.str();
}
