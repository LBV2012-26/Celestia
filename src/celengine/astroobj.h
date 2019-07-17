#pragma once
#include <memory>
#include <unordered_set>
#include <celengine/selection.h>
#include <celengine/parseobject.h>
#include "astrocat.h"
#include "name.h"

class AstroDatabase;

class UserCategory;

class AstroObject {
    AstroCatalog::IndexNumber m_mainIndexNumber { AstroCatalog::InvalidIndex };
    AstroDatabase *m_db { nullptr };
public:
    AstroObject() = default;
    AstroObject(AstroDatabase *db, AstroCatalog::IndexNumber nr = AstroCatalog::InvalidIndex) : m_db(db), m_mainIndexNumber(nr) {}
    AstroCatalog::IndexNumber getIndex() const { return m_mainIndexNumber; }
    void setIndex(AstroCatalog::IndexNumber);
    AstroDatabase *getAstroDatabase() { return m_db; }

// Names support

 protected:
    SharedConstNameInfoSet m_nameInfos;
    NameInfo::SharedConstPtr m_primaryName;
    SharedConstNameInfoSet::iterator getNameInfoIterator(const Name &) const;
 public:
    bool addName(const std::string&, const std::string& = std::string(), bool = true, bool = true);
    bool addName(const Name&, const std::string& = std::string(), bool = true, bool = true);
    bool addName(NameInfo::SharedConstPtr, bool = true, bool = true);
    void addNames(const std::string&, bool = true);
    bool addAlias(const std::string& name, const std::string& domain = string()) { return addName(name, domain, false); }
    bool addAlias(NameInfo::SharedConstPtr info) { return addName(info, false); }
    const Name getName(bool i18n = false) const;
    const Name getLocalizedName() const;
    bool hasName(const Name& name) const;
    bool hasName(const std::string& name) const;
    bool hasName() const { return m_primaryName && !m_primaryName->getCanon().empty(); }
    bool hasLocalizedName(const Name& name) const;
    bool hasLocalizedName(const std::string& name) const;
    bool hasLocalizedName() { return m_primaryName && m_primaryName->hasLocalized(); }
    const SharedConstNameInfoSet& getNameInfos() const { return m_nameInfos; }
    bool removeName(const std::string&, bool = true);
    bool removeName(const Name&, bool = true);
    bool removeName(NameInfo::SharedConstPtr, bool = true);
    void removeNames(bool = true);
    NameInfo::SharedConstPtr getNameInfo(const Name &name) const;
    std::string getNames(bool = true) const;

// Part from legacy CatEntry
public:
    typedef std::unordered_set<UserCategory*> CategorySet;

private:
    CategorySet *m_cats { nullptr };
protected:
    bool _addToCategory(UserCategory*);
    bool _removeFromCategory(UserCategory*);
    void setDatabase(AstroDatabase *db)
    {
        m_db = db;
    }
public:
    virtual Selection toSelection();
    bool addToCategory(UserCategory*);
    bool addToCategory(const std::string&, bool = false, const std::string &domain = "");
    bool removeFromCategory(UserCategory*);
    bool removeFromCategory(const std::string&);
    bool clearCategories();
    bool isInCategory(UserCategory*) const;
    bool isInCategory(const std::string&) const;
    int categoriesCount() const { return m_cats == nullptr ? 0 : m_cats->size(); }
    CategorySet *getCategories() const { return m_cats; };
    bool loadCategories(Hash*, DataDisposition = DataDisposition::Add, const std::string &domain = "");
    friend UserCategory;
    friend AstroDatabase;
};
