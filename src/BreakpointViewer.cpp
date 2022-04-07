#include "BreakpointViewer.h"
#include "Convert.h"
#include "CommClient.h"
#include "OpenMSXConnection.h"
#include "ScopedAssign.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMessageBox>
#include <QToolTip>
#include <QComboBox>
#include <cassert>

static constexpr int UNDEFINED_ROW = -1;

enum TableColumns {
	ENABLED = 0,
	WP_TYPE = 1,
	LOCATION = 2,
	BP_ADDRESS = 2,
	WP_REGION = 2,
	T_CONDITION = 3,
	SLOT = 4,
	SEGMENT = 5,
	ID = 6
};

BreakpointViewer::BreakpointViewer(QWidget* parent)
	: QTabWidget(parent),
	  ui(new Ui::BreakpointViewer)
{
	setupUi(this);

	bpTableWidget->horizontalHeader()->setHighlightSections(false);
	bpTableWidget->sortByColumn(BP_ADDRESS, Qt::AscendingOrder);
	bpTableWidget->setColumnHidden(WP_TYPE, true);
	bpTableWidget->setColumnHidden(ID, true);
	bpTableWidget->resizeColumnsToContents();
	bpTableWidget->setSortingEnabled(true);
	connect(bpTableWidget, &QTableWidget::itemPressed, this, &BreakpointViewer::on_itemPressed);
	connect(bpTableWidget, &QTableWidget::itemChanged, this, &BreakpointViewer::changeBpTableItem);
	connect(bpTableWidget->horizontalHeader(), &QHeaderView::sectionClicked, this,
	        &BreakpointViewer::on_headerClicked);

	wpTableWidget->horizontalHeader()->setHighlightSections(false);
	wpTableWidget->setColumnHidden(ID, true);
	wpTableWidget->sortByColumn(WP_REGION, Qt::AscendingOrder);
	wpTableWidget->resizeColumnsToContents();
	wpTableWidget->setSortingEnabled(true);
	connect(wpTableWidget, &QTableWidget::itemChanged, this,
	        &BreakpointViewer::changeWpTableItem);

	cnTableWidget->horizontalHeader()->setHighlightSections(false);
	cnTableWidget->setColumnHidden(WP_TYPE, true);
	cnTableWidget->setColumnHidden(LOCATION, true);
	cnTableWidget->setColumnHidden(SLOT, true);
	cnTableWidget->setColumnHidden(SEGMENT, true);
	cnTableWidget->setColumnHidden(ID, true);
	cnTableWidget->sortByColumn(T_CONDITION, Qt::AscendingOrder);
	cnTableWidget->resizeColumnsToContents();
	cnTableWidget->setSortingEnabled(true);
	connect(cnTableWidget, &QTableWidget::itemChanged, this,
	        &BreakpointViewer::changeCnTableItem);

	tables[BreakpointRef::BREAKPOINT] = bpTableWidget;
	tables[BreakpointRef::WATCHPOINT] = wpTableWidget;
	tables[BreakpointRef::CONDITION]  = cnTableWidget;
}

// TODO: move the createSetCommand to a session manager
void BreakpointViewer::createBreakpoint(BreakpointRef::Type type, int row)
{
	if (type == BreakpointRef::CONDITION) _createCondition(row);
	else _createBreakpoint(type, row);
}

void BreakpointViewer::_handleSyncError(const QString& error)
{
	int choice = QMessageBox::warning(nullptr, tr("Synchronization error"),
			tr("Error message from openMSX:\n\"%1\"\n\nClick on Reset to resynchronize with openMSX")
			.arg(error.trimmed()), QMessageBox::Reset | QMessageBox::Cancel);
	if (choice == QMessageBox::Reset) {
		auto sa = ScopedAssign(selfUpdating, false);
		emit contentsUpdated(false);
	}
}

void BreakpointViewer::_handleKeyAlreadyExists()
{
	int choice = QMessageBox::warning(nullptr, tr("Reference error"),
			tr("A breakpoint with the same name already exists locally. This is probably "
			   "a synchronization problem.\n\nClick on Reset to resynchronize with openMSX"),
			QMessageBox::Reset | QMessageBox::Cancel);
	if (choice == QMessageBox::Reset) {
		auto sa = ScopedAssign(selfUpdating, false);
		emit contentsUpdated(false);
	}
}

void BreakpointViewer::_handleKeyNotFound()
{
	int choice = QMessageBox::warning(nullptr, tr("Reference error"),
			tr("Breakpoint was not found locally. This is probably a synchronization problem.\n\n"
			   "Click on Reset to resynchronize with openMSX"),
			QMessageBox::Reset | QMessageBox::Cancel);
	if (choice == QMessageBox::Reset) {
		auto sa = ScopedAssign(selfUpdating, false);
		emit contentsUpdated(false);
	}
}

void BreakpointViewer::_createBreakpoint(BreakpointRef::Type type, int row)
{
	assert(type != BreakpointRef::CONDITION);
	auto* table = tables[type];
	auto* model = table->model();
	auto* combo = (QComboBox*) table->indexWidget(model->index(row, WP_TYPE));
	Breakpoint::Type wtype = type == BreakpointRef::WATCHPOINT ? readComboBox(row) : Breakpoint::BREAKPOINT;

	QString location = table->item(row, LOCATION)->text();
	auto loc = parseLocationField(-1, type, location, combo ? combo->currentText() : "");
	setBreakpointChecked(type, row, loc ? Qt::Checked : Qt::Unchecked);
	if (!loc) return;
	auto [start, end] = *loc;

	QString condition = table->item(row, T_CONDITION)->text();

	auto ps_ss = parseSlotField(-1, table->item(row, SLOT)->text());
	assert(ps_ss);
	auto [ps, ss] = *ps_ss;

	auto segment = parseSegmentField(-1, table->item(row, SEGMENT)->text());
	assert(segment);

	const QString cmdStr = Breakpoints::createSetCommand(wtype, start, ps, ss, *segment, end, condition);

	auto* command = new Command(cmdStr,
		[this, type, row] (const QString& id) {
			setTextField(type, row, ID, id);

			BreakpointRef ref{type, id, row, -1};
			if (!connectBreakpointID(id, ref)) {
				setBreakpointChecked(type, row, Qt::Unchecked);
				_handleKeyAlreadyExists();
				return;
			}

			auto sa = ScopedAssign(selfUpdating, true);
			emit contentsUpdated(false);
		},
		[this] (const QString& error) { _handleSyncError(error); }
	);

	CommClient::instance().sendCommand(command);
}

// TODO: move the createRemoveCommand to a session manager
void BreakpointViewer::replaceBreakpoint(BreakpointRef::Type type, int row)
{
	auto* table = tables[type];
	auto* item  = table->item(row, ID);
	QString id  = item->text();

	const QString cmdStr = breakpoints->createRemoveCommand(id);
	auto* command = new Command(cmdStr,
		[this, type, row] (const QString& /*result*/) {
			createBreakpoint(type, row);
		},
		[this](const QString& error) { _handleSyncError(error); }
	);

	CommClient::instance().sendCommand(command);
}

// TODO: move the createRemoveCommand to a session manager
void BreakpointViewer::removeBreakpoint(BreakpointRef::Type type, int row, bool logical)
{
	auto* table = tables[type];

	auto* item  = table->item(row, ID);
	assert(!item->text().isEmpty());
	QString id  = item->text();

	const QString cmdStr = Breakpoints::createRemoveCommand(id);

	auto* command = new Command(cmdStr,
		[this, type, row, id, logical] (const QString& /*result*/) {
			size_t erased = maps[type].erase(id);

			if (erased != 1) {
				setBreakpointChecked(type, row, Qt::Unchecked);
				_handleKeyNotFound();
				return;
			}

			if (!logical) {
				auto* table = tables[type];
				auto     sa = ScopedAssign(userMode, false);
				table->removeRow(row);
			}

			auto sa = ScopedAssign(selfUpdating, true);
			emit contentsUpdated(false);
		},
		[this] (const QString& error) { _handleSyncError(error); }
	);

	CommClient::instance().sendCommand(command);
}

// TODO: move the createSetCommand to a session manager
void BreakpointViewer::_createCondition(int row)
{
	QString condition = cnTableWidget->item(row, T_CONDITION)->text();
	if (condition.isEmpty()) {
		setBreakpointChecked(BreakpointRef::CONDITION, row, Qt::Unchecked);
		return;
	} else {
		setBreakpointChecked(BreakpointRef::CONDITION, row, Qt::Checked);
	}

	const QString cmdStr = Breakpoints::createSetCommand(Breakpoint::CONDITION, -1, -1, -1, -1, -1, condition);

	auto* command = new Command(cmdStr,
		[this, row] (const QString& id) {
			setTextField(BreakpointRef::CONDITION, row, ID, id);

			BreakpointRef ref{BreakpointRef::CONDITION, id, row, -1};
			if (!connectBreakpointID(id, ref)) {
				_handleKeyAlreadyExists();
				return;
			}

			auto sa = ScopedAssign(selfUpdating, true);
			emit contentsUpdated(false);
		},
		[this] (const QString& error) { _handleSyncError(error); }
	);

	CommClient::instance().sendCommand(command);
}

void BreakpointViewer::setBreakpointChecked(BreakpointRef::Type type, int row, Qt::CheckState state)
{
	auto sa = ScopedAssign(userMode, false);

	auto* table    = tables[type];
	bool  oldValue = table->isSortingEnabled();
	auto* item     = table->item(row, ENABLED);
	assert(item);

	table->setSortingEnabled(false);
	item->setCheckState(state);
	table->setSortingEnabled(oldValue);
}

void BreakpointViewer::setTextField(BreakpointRef::Type type, int row, int column, const QString& value)
{
	auto sa = ScopedAssign(userMode, false);

	auto* table    = tables[type];
	bool  oldValue = table->isSortingEnabled();
	auto* item     = table->item(row, column);

	table->setSortingEnabled(false);
	item->setText(value);
	table->setSortingEnabled(oldValue);
}

std::optional<Slot> BreakpointViewer::parseSlotField(int index, const QString& field)
{
	QStringList s = field.simplified().split("/", Qt::SplitBehaviorFlags::SkipEmptyParts);

	qint8 ps;
	if (s[0].compare("X", Qt::CaseInsensitive)) {
		bool ok;
		int value = s[0].toInt(&ok);
		if (!ok || !(0 <= value && value <= 3)) {
			// restore value
			if (index != -1) {
				ps = breakpoints->getBreakpoint(index).ps;
			} else {
				return {};
			}
		} else {
			ps = qint8(value);
		}
	} else {
		ps = -1;
	}

	qint8 ss;
	if (s.size() == 2 && s[1].compare("X", Qt::CaseInsensitive)) {
		bool ok;
		int value = s[1].toInt(&ok);
		if (!ok || !(0 <= value && value <= 3)) {
			if (index != -1) {
				ss = breakpoints->getBreakpoint(index).ss;
			} else {
				return {};
			}
		} else {
			ss = qint8(value);
		}
	} else {
		ss = -1;
	}
	return Slot{ps, ss};
}

std::optional<qint16> BreakpointViewer::parseSegmentField(int index, const QString& field)
{
	QString s = field.simplified();

	if (s.compare("X", Qt::CaseInsensitive)) {
		bool ok;
		int value = field.toInt(&ok);
		if (!ok || !(0 <= value && value <= 255)) {
			if (index != -1) {
				return breakpoints->getBreakpoint(index).segment;
			} else {
				return {};
			}
		} else {
			return value;
		}
	} else {
		return -1;
	}
}

static const char* ComboTypeNames[] = { "read_mem", "write_mem", "read_io", "write_io" };

std::optional<AddressRange> BreakpointViewer::parseLocationField(
	int index, BreakpointRef::Type type, const QString& field, const QString& comboTxt)
{
	if (type == BreakpointRef::BREAKPOINT) {
		if (int value = stringToValue(field.simplified()); value > -1 && value < 0x10000) {
			return AddressRange{value, value};
		} else if (index != -1) {
			auto address = breakpoints->getBreakpoint(index).address;
			return AddressRange{address, address};
		}
		return {};
	}

	QStringList s = field.simplified().split(":", Qt::SplitBehaviorFlags::SkipEmptyParts);
	int begin = (s.size() >= 1) ? stringToValue(s[0]) : -1;
	int end   = (s.size() == 2) ? stringToValue(s[1]) : (s.size() == 1 ? begin : -1);

	auto iter  = std::find(std::begin(ComboTypeNames), std::end(ComboTypeNames), comboTxt);
	auto wtype = static_cast<Breakpoint::Type>(std::distance(ComboTypeNames, iter) + 1);
	if ((wtype == Breakpoint::WATCHPOINT_IOREAD || wtype == Breakpoint::WATCHPOINT_IOWRITE)
	     && (begin > 0xFF || end > 0xFF)) {
		return {};
	}
	if (begin != -1 && end != -1 && end >= begin) return AddressRange{begin, end};

	if (index == -1) return {};
	const auto& bp = breakpoints->getBreakpoint(index);
	return AddressRange{bp.address, bp.regionEnd};
}

void BreakpointViewer::changeTableItem(BreakpointRef::Type type, QTableWidgetItem* item)
{
	auto* table = tables[type];
	int   row   = table->row(item);

	// trying to modify a bp instead of creating a new one
	bool  createBp = false;
	auto  ref      = findBreakpointRef(type, row);
	int   index    = ref ? ref->index : -1;

	// check if breakpoint is enabled
	auto* enabledItem = table->item(row, ENABLED);
	bool  enabled     = enabledItem->checkState() == Qt::Checked;

	switch (table->column(item)) {
		case ENABLED:
			createBp = enabled;
			break;
		case WP_TYPE:
			assert(table == wpTableWidget);
			item = table->item(row, LOCATION);
			[[fallthrough]];
		case LOCATION: {
			auto* model = table->model();
			auto* combo = (QComboBox*) table->indexWidget(model->index(row, WP_TYPE));
			int   adrLen;

			if (type == BreakpointRef::CONDITION) {
				return;
			} else if (type == BreakpointRef::WATCHPOINT) {
				auto wType = static_cast<Breakpoint::Type>(combo->currentIndex() + 1);
				adrLen = (wType == Breakpoint::WATCHPOINT_IOREAD || wType == Breakpoint::WATCHPOINT_IOWRITE)
					? 2 : 4;
			} else {
				adrLen = 4;
			}

			if (auto loc = parseLocationField(index, type, item->text(), combo ? combo->currentText() : "")) {
				auto [begin, end] = *loc;
				setTextField(type, row, LOCATION, QString("%1%2%3").arg(hexValue(begin, adrLen))
					.arg(end == begin || end == -1 ? "" : ":")
					.arg(end == begin || end == -1 ? "" : hexValue(end, adrLen)));
			} else {
				enabled = false;
				setTextField(type, row, LOCATION, "");
				setBreakpointChecked(type, row, Qt::Unchecked);
			}
			if (!enabled) return;
			break;
		}
		case SLOT: {
			if (auto ps_ss = parseSlotField(index, item->text())) {
				auto [ps, ss] = *ps_ss;
				setTextField(type, row, SLOT, QString("%1/%2")
					.arg(ps == -1 ? QChar('X') : QChar('0' + ps))
					.arg(ss == -1 ? QChar('X') : QChar('0' + ss)));
			} else {
				setTextField(type, row, SLOT, "X/X");
			}
			if (!enabled) return;
			break;
		}
		case SEGMENT: {
			if (auto segment = parseSegmentField(index, item->text())) {
				setTextField(type, row, SEGMENT, QString("%1").arg(*segment == -1 ? "X" : QString::number(*segment)));
			} else {
				setTextField(type, row, SEGMENT, "X");
			}
			if (!enabled) return;
			break;
		}
		case T_CONDITION: {
			setTextField(type, row, T_CONDITION, item->text().simplified());

			if (type == BreakpointRef::CONDITION) {
				setBreakpointChecked(type, row, item->text().isEmpty() ? Qt::Unchecked : item->checkState());
			}
			if (!enabled) return;
			break;
		}
		case ID:
			return;
		default:
			qWarning() << "Unknown table column" << table->column(item);
			assert(false);
	}

	if (enabled) {
		if (createBp) {
			createBreakpoint(type, row);
		} else {
			replaceBreakpoint(type, row);
		}
	} else {
		if (index != -1) removeBreakpoint(type, row, true);
	}
}

void BreakpointViewer::disableSorting(BreakpointRef::Type type)
{
	auto unsort = [](auto* table) {
		table->sortByColumn(-1, Qt::AscendingOrder);
	};

	if (type == BreakpointRef::ALL) {
		for (auto* table : tables) {
			unsort(table);
		}
	} else {
		unsort(tables[type]);
	}
}

void BreakpointViewer::changeBpTableItem(QTableWidgetItem* item)
{
	if (!userMode) return;
	disableSorting(BreakpointRef::BREAKPOINT);
	changeTableItem(BreakpointRef::BREAKPOINT, item);
}

void BreakpointViewer::changeWpTableItem(QTableWidgetItem* item)
{
	if (!userMode) return;
	disableSorting(BreakpointRef::WATCHPOINT);
	changeTableItem(BreakpointRef::WATCHPOINT, item);
}

void BreakpointViewer::changeCnTableItem(QTableWidgetItem* item)
{
	if (!userMode) return;
	disableSorting(BreakpointRef::CONDITION);
	changeTableItem(BreakpointRef::CONDITION, item);
}

void BreakpointViewer::setBreakpoints(Breakpoints* bps)
{
	breakpoints = bps;
}

void BreakpointViewer::setRunState()
{
	runState = true;
}

void BreakpointViewer::setBreakState()
{
	runState = false;
}

void BreakpointViewer::stretchTable(BreakpointRef::Type type)
{
	auto stretch = [](auto* table) {
		// stretching will not work without sorting
		table->setSortingEnabled(true);
		table->resizeColumnsToContents();
	};

	if (type == BreakpointRef::ALL) {
		for (auto* table : tables) {
			stretch(table);
		}
	} else {
		stretch(tables[type]);
	}
}

BreakpointRef* BreakpointViewer::scanBreakpointRef(const Breakpoint& bp)
{
	BreakpointRef::Type type = bp.type == Breakpoint::BREAKPOINT ? BreakpointRef::BREAKPOINT
	                        : (bp.type == Breakpoint::CONDITION  ? BreakpointRef::CONDITION : BreakpointRef::WATCHPOINT);

	// assumed premise: a new bp will never repeat same name again
	if (auto it = maps[type].find(bp.id); it != maps[type].end() && it->second.index == -1) {
		// assumed premise 2: if name remains the same, bp is exactly the same.
		return &it->second;
	}

	return nullptr;
}

bool BreakpointViewer::connectBreakpointID(const QString& id, BreakpointRef& ref)
{
	auto [_, result] = maps[ref.type].try_emplace(id, ref);
	return result;
}

void BreakpointViewer::sync()
{
	// don't reload if self-inflicted update
	if (selfUpdating) return;

	bool empty[BreakpointRef::ALL] = {false, false, false};

	for (int type = 0; type < BreakpointRef::ALL; ++type) {
		if (maps[type].empty()) empty[type] = true;

		for (auto& [_, ref] : maps[type]) {
			ref.index = -1;
			ref.row = -1;
		}
	}

	for (int index = 0; index < breakpoints->breakpointCount(); ++index) {
		const Breakpoint& bp = breakpoints->getBreakpoint(index);

		BreakpointRef::Type type = bp.type == Breakpoint::BREAKPOINT ? BreakpointRef::BREAKPOINT
		                         : (bp.type == Breakpoint::CONDITION  ? BreakpointRef::CONDITION : BreakpointRef::WATCHPOINT);

		if (!empty[type]) {
			if (auto* ref = scanBreakpointRef(bp)) {
				ref->index = index;
				continue;
			}
		}

		// create new BreakpointRef
		BreakpointRef ref{type, bp.id, UNDEFINED_ROW, index};
		bool result = connectBreakpointID(bp.id, ref);
		assert(result);
	}

	// remove missing BreakpointRef
	for (auto& map : maps) {
		auto it = map.begin();

		while (it != map.end()) {
			if (it->second.index == -1) {
				auto oldit = it++;
				map.erase(oldit->second.id);
			} else {
				it++;
			}
		}
	}

	populate();
}

void BreakpointViewer::populate()
{
	// store unused items position by disabling ordering
	disableSorting();

	// remember selected rows
	int bpFocus = bpTableWidget->currentRow();
	int wpFocus = wpTableWidget->currentRow();
	int cnFocus = cnTableWidget->currentRow();

	// reuse current table rows
	for (int index = 0; index < BreakpointRef::ALL; ++index) {
		auto type = static_cast<BreakpointRef::Type>(index);
		auto* table = tables[index];

		for (int row = 0; row < table->rowCount(); ++row) {
			// reuse existing table row
			if (auto* ref = scanBreakpointRef(type, row)) {
				fillTableRow(ref->index, type, row);
				ref->row = row;
			} else {
				setBreakpointChecked(type, row, Qt::Unchecked);
			}
		}
	}

	// add only new breakpoints
	for (auto& map : maps) {
		for (auto& [_, ref] : map) {
			if (ref.row != -1) continue;
			ref.row = createTableRow(ref.type);
			fillTableRow(ref.index, ref.type, ref.row);
		}
	}

	stretchTable(BreakpointRef::ALL);

	bpTableWidget->selectRow(bpFocus);
	wpTableWidget->selectRow(wpFocus);
	cnTableWidget->selectRow(cnFocus);
}

BreakpointRef* BreakpointViewer::findBreakpointRef(BreakpointRef::Type type, int row)
{
	auto& table = tables[type];
	auto* idItem = table->item(row, ID);
	auto it = maps[type].find(idItem->text());
	if (it != maps[type].end()) return &it->second;
	return nullptr;
}

BreakpointRef* BreakpointViewer::scanBreakpointRef(BreakpointRef::Type type, int row)
{
	auto tmp = parseTableRow(type, row);
	if (!tmp) return nullptr;

	for (auto& [_, ref] : maps[type]) {
		const Breakpoint& bp = breakpoints->getBreakpoint(ref.index);
		// exact match
		if (tmp->id == bp.id) return &ref;

		// otherwise, try to find by similarity
		if (tmp->type == bp.type && tmp->address == bp.address
		    && (tmp->regionEnd == bp.regionEnd || (tmp->regionEnd == tmp->address && bp.regionEnd == bp.address))
		    && (tmp->segment == -1 || tmp->segment == bp.segment) && tmp->condition == bp.condition) {
			return &ref;
		}
	}

	return nullptr;
}

void BreakpointViewer::changeCurrentWpType(int row, int /*selected*/)
{
	if (!userMode) return;
	auto* item = wpTableWidget->item(row, WP_TYPE);
	changeTableItem(BreakpointRef::WATCHPOINT, item);
}

void BreakpointViewer::createComboBox(int row)
{
	auto sa = ScopedAssign(userMode, false);

	auto* combo = new QComboBox();
	combo->setEditable(false);
	combo->insertItems(0, QStringList(std::begin(ComboTypeNames), std::end(ComboTypeNames)));

	auto* model = wpTableWidget->model();
	auto  index = model->index(row, WP_TYPE);

	wpTableWidget->setIndexWidget(index, combo);

	connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
		[this, row](int index){ changeCurrentWpType(row, index); });
}

Breakpoint::Type BreakpointViewer::readComboBox(int row)
{
	auto* model = wpTableWidget->model();
	auto* combo = (QComboBox*) wpTableWidget->indexWidget(model->index(row, WP_TYPE));
	return static_cast<Breakpoint::Type>(combo->currentIndex() + 1);
}

int BreakpointViewer::createTableRow(BreakpointRef::Type type, int row)
{
	auto     sa = ScopedAssign(userMode, false);
	auto& table = tables[type];

	if (row == -1) {
		row = table->rowCount();
		table->setRowCount(row + 1);
		table->selectRow(row);
	} else {
		table->insertRow(row);
		table->selectRow(row);
	}

	// enabled
	auto* item0 = new QTableWidgetItem();
	item0->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
	item0->setTextAlignment(Qt::AlignCenter);
	table->setItem(row, ENABLED, item0);

	// watchpoint type
	auto* item1 = new QTableWidgetItem();
	item1->setTextAlignment(Qt::AlignCenter);
	table->setItem(row, WP_TYPE, item1);

	if (type == BreakpointRef::WATCHPOINT) {
		createComboBox(row);
	}

	// address
	auto* item2 = new QTableWidgetItem();
	item2->setTextAlignment(Qt::AlignCenter);
	table->setItem(row, LOCATION, item2);

	if (type == BreakpointRef::CONDITION) {
		item2->setFlags(Qt::NoItemFlags);
		item2->setText("0");
	} else {
		item2->setText("");
	}

	// condition
	auto* item3 = new QTableWidgetItem();
	item3->setTextAlignment(Qt::AlignCenter);
	item3->setText("");
	table->setItem(row, T_CONDITION, item3);

	// ps/ss
	auto* item4 = new QTableWidgetItem();
	item4->setTextAlignment(Qt::AlignCenter);
	item4->setText("X/X");
	table->setItem(row, SLOT, item4);

	// segment
	auto* item5 = new QTableWidgetItem();
	item5->setTextAlignment(Qt::AlignCenter);
	item5->setText("X");
	table->setItem(row, SEGMENT, item5);

	// index
	auto* item6 = new QTableWidgetItem();
	item6->setFlags(Qt::NoItemFlags);
	item6->setText("");
	table->setItem(row, ID, item6);

	return row;
}

void BreakpointViewer::fillTableRow(int index, BreakpointRef::Type type, int row)
{
	auto sa = ScopedAssign(userMode, false);

	const auto& bp = breakpoints->getBreakpoint(index);
	auto& table = tables[type];

	// enabled status
	setBreakpointChecked(type, row, Qt::Checked);

	// watchpoint type
	if (type == BreakpointRef::WATCHPOINT) {
	        auto* model = table->model();
		auto* combo = (QComboBox*) table->indexWidget(model->index(row, WP_TYPE));
		combo->setCurrentIndex(static_cast<int>(bp.type) - 1);
	}

	// location
	int locLen = (bp.type == Breakpoint::WATCHPOINT_IOREAD
	           || bp.type == Breakpoint::WATCHPOINT_IOWRITE) ? 2 : 4;
	QString address   = hexValue(bp.address, locLen);
	QString regionEnd = hexValue(bp.regionEnd, locLen);
	QString location  = QString("%1%2%3").arg(address)
		.arg(regionEnd == -1 || regionEnd == address ? "" : ":")
		.arg(regionEnd == -1 || regionEnd == address ? "" : regionEnd);

	// location
	auto* item2 = table->item(row, LOCATION);
	item2->setText(location);

	auto* item3 = table->item(row, T_CONDITION);
	item3->setText(bp.condition);

	// slot
	QString slot = QString("%1/%2")
		.arg(bp.ps == -1 ? QChar('X') : QChar('0' + bp.ps))
		.arg(bp.ss == -1 ? QChar('X') : QChar('0' + bp.ss));

	auto* item4 = table->item(row, SLOT);
	item4->setText(slot);

	// segment
	QString segment = (bp.segment == -1 ? "X" : QString("%1").arg(bp.segment));

	auto* item5 = table->item(row, SEGMENT);
	item5->setText(segment);

	// id
	auto* item6 = table->item(row, ID);
	item6->setText(bp.id);
}

std::optional<Breakpoint> BreakpointViewer::parseTableRow(BreakpointRef::Type type, int row)
{
	Breakpoint bp;

	auto* table = tables[type];
	auto* model = table->model();
	auto* combo = (QComboBox*) table->indexWidget(model->index(row, WP_TYPE));
	bp.type = type == BreakpointRef::WATCHPOINT ? readComboBox(row) : Breakpoint::BREAKPOINT;

	QString location = table->item(row, LOCATION)->text();
	auto loc = parseLocationField(-1, type, location, combo ? combo->currentText() : "");
	if (!loc) return {};
	bp.address = loc->start;
	bp.regionEnd = loc->end;

	QString condition = table->item(row, T_CONDITION)->text();

	auto slot = parseSlotField(-1, table->item(row, SLOT)->text());
	if (!slot) return {};
    bp.ps = slot->ps;
    bp.ss = slot->ss;

	auto segment = parseSegmentField(-1, table->item(row, SEGMENT)->text());
	if (!segment) return {};
	bp.segment = *segment;

	return bp;
}

void BreakpointViewer::onAddBtnClicked(BreakpointRef::Type type)
{
	auto sa = ScopedAssign(userMode, false);

	auto* table = tables[type];
	table->sortByColumn(-1, Qt::AscendingOrder);
	table->setSortingEnabled(false);

	int row = createTableRow(type, 0);
	setBreakpointChecked(type, row, Qt::Unchecked);
	stretchTable(type);
	table->setSortingEnabled(true);
}

void BreakpointViewer::onRemoveBtnClicked(BreakpointRef::Type type)
{
	auto* table = tables[type];

	if (table->currentRow() == -1)
		return;

	auto sa = ScopedAssign(userMode, false);
	QTableWidgetItem* item = table->item(table->currentRow(), ENABLED);
	if (item->checkState() == Qt::Checked) {
		removeBreakpoint(type, table->currentRow(), false);
	} else {
		table->removeRow(table->currentRow());
	}
}

void BreakpointViewer::on_btnAddBp_clicked()
{
	onAddBtnClicked(BreakpointRef::BREAKPOINT);
}

void BreakpointViewer::on_btnRemoveBp_clicked()
{
	onRemoveBtnClicked(BreakpointRef::BREAKPOINT);
}

void BreakpointViewer::on_btnAddWp_clicked()
{
	onAddBtnClicked(BreakpointRef::WATCHPOINT);
}

void BreakpointViewer::on_btnRemoveWp_clicked()
{
	onRemoveBtnClicked(BreakpointRef::WATCHPOINT);
}

void BreakpointViewer::on_btnAddCn_clicked()
{
	onAddBtnClicked(BreakpointRef::CONDITION);
}

void BreakpointViewer::on_btnRemoveCn_clicked()
{
	onRemoveBtnClicked(BreakpointRef::CONDITION);
}

void BreakpointViewer::on_itemPressed(QTableWidgetItem* /*item*/)
{
	bpTableWidget->setSortingEnabled(false);
}

void BreakpointViewer::on_headerClicked(int /*index*/)
{
	bpTableWidget->setSortingEnabled(true);
}
