#!/usr/bin/env bash
#
# source.db_pointers_initialised — a `Database *` that is declared is a
# `Database *` that is initialised.
#
# THE CLASS (lane DBPTR-1, found by CLOSE-2's second read): 24 members across
# src/ were declared `Database *db;` with no initialiser. Most were filled by a
# constructor, but seven were filled by a SETTER called after construction —
# and the widgets' own signals can reach a dereference before that setter runs.
# Two of them were never assigned at all (ShaderAssetWidget, whose setter
# stored the handle only when a scene happened to be open at construction, and
# WorldPropertyWidget, whose setter had no caller in the tree). They survived
# because the queries they reached touch no member of Database — the same
# accident that hid AssetPanel::handle, which DID crash the moment a member
# read was added to that funnel (CLOSE-2).
#
# So: every declaration carries `= nullptr`, and the dereference sites carry a
# truthful fallback. This gate keeps a future one from growing back.
#
# $1 = the repo root
set -u

ROOT="${1:?usage: db_pointers_initialised.sh <source-root>}"
cd "$ROOT" || { echo "source.db_pointers_initialised: no such root $ROOT"; exit 1; }

failures=0

# `Database *x;` / `QSqlDatabase *x;` with nothing after the name. A parameter
# never matches (it ends in `,` or `)`), an initialised member never matches
# (`= nullptr;`). Lines whose first non-space characters are // or * are prose.
decl='(^|[^:_[:alnum:]])(Database|QSqlDatabase)[[:space:]]*\*[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*;'

hits=$(grep -rnE "$decl" src/ 2>/dev/null \
       | grep -vE ':[0-9]+:[[:space:]]*(//|\*|/\*)' || true)

if [ -n "$hits" ]; then
    echo "FAIL: a Database pointer is declared without an initialiser."
    echo "      Write '= nullptr' on the declaration — and, where a setter fills"
    echo "      it after construction, give every dereference a truthful"
    echo "      fallback (see the header of this script)."
    echo "$hits"
    failures=$((failures + 1))
else
    echo "ok:   every Database/QSqlDatabase pointer declaration carries an initialiser"
fi

# The two handles that are filled by a setter AND read from a signal: the
# forwarding that feeds them must stay wired, or the feature goes quiet again.
if grep -q 'worldPropView->setDatabase(db)' src/ui/panels/scenenodepropertieswidget.cpp; then
    echo "ok:   the World blade is still handed the library"
else
    echo "FAIL: SceneNodePropertiesWidget::setDatabase no longer forwards to the"
    echo "      World blade — its Background Ambience row needs the library."
    failures=$((failures + 1))
fi

if grep -qE '^\s*this->db = db;' src/modules/materials/widgets/shaderassetwidget.cpp; then
    echo "ok:   ShaderAssetWidget stores its handle unconditionally"
else
    echo "FAIL: ShaderAssetWidget::setUpDatabase no longer stores the handle"
    echo "      unconditionally — it is called before the scene-open probe exists."
    failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
    echo "source.db_pointers_initialised: $failures failure(s)"
    exit 1
fi
echo "source.db_pointers_initialised: ok"
