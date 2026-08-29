#ifndef SHADERGRAPH_EXPORTER
#define SHADERGRAPH_EXPORTER

#include <QJsonDocument>
#include <QString>
#include "zip.h"
#include "src/core/database/database.h"
#include "src/core/assethelper.h"
#include "src/shadergraph/core/materialhelper.h"

class Exporter
{
public:
	static void exportShaderAsMaterial(Database* dataBase, QString shaderGuid, QString filePath)
	{
		QTemporaryDir temporaryDir;
		if (!temporaryDir.isValid()) return;

		const QString writePath = temporaryDir.path();

		//const QString guid = assetItem.wItem->data(MODEL_GUID_ROLE).toString();
		auto materialGuid = generateMaterialFromShader(shaderGuid, dataBase, writePath);
		dataBase->createBlobFromAsset(materialGuid, QDir(writePath).filePath("asset.db"));

		QDir tempDir(writePath);
		tempDir.mkpath("assets");

		QFile manifest(QDir(writePath).filePath(".manifest"));
		if (manifest.open(QIODevice::ReadWrite)) {
			QTextStream stream(&manifest);
			stream << "material";
		}
		manifest.close();

		for (const auto &assetGuid : AssetHelper::fetchAssetAndAllDependencies(materialGuid, dataBase)) {
			auto asset = dataBase->fetchAsset(assetGuid);
			//auto assetPath = QDir(Globals::project->getProjectFolder()).filePath(asset.name);
			auto assetPath = getAssetPath(asset);
			QFileInfo assetInfo(assetPath);
			if (assetInfo.exists()) {
				QFile::copy(
					IrisUtils::join(assetPath),
					IrisUtils::join(writePath, "assets", assetInfo.fileName())
				);
			}
		}

		// get all the files and directories in the project working directory
		QDir workingProjectDirectory(writePath);
		QDirIterator projectDirIterator(
			writePath,
			QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs | QDir::Hidden, QDirIterator::Subdirectories
		);

		QVector<QString> fileNames;
		while (projectDirIterator.hasNext()) fileNames.push_back(projectDirIterator.next());

		// open a basic zip file for writing, maybe change compression level later (iKlsR)
		struct zip_t *zip = zip_open(filePath.toStdString().c_str(), ZIP_DEFAULT_COMPRESSION_LEVEL, 'w');

		for (int i = 0; i < fileNames.count(); i++) {
			QFileInfo fInfo(fileNames[i]);

			// we need to pay special attention to directories since we want to write empty ones as well
			if (fInfo.isDir()) {
				zip_entry_open(
					zip,
					/* will only create directory if / is appended */
					QString(workingProjectDirectory.relativeFilePath(fileNames[i]) + "/").toStdString().c_str()
				);
				zip_entry_fwrite(zip, fileNames[i].toStdString().c_str());
			}
			else {
				zip_entry_open(
					zip,
					workingProjectDirectory.relativeFilePath(fileNames[i]).toStdString().c_str()
				);
				zip_entry_fwrite(zip, fileNames[i].toStdString().c_str());
			}

			// we close each entry after a successful write
			zip_entry_close(zip);
		}

		// close our now exported file
		zip_close(zip);
	}

	static QString generateMaterialFromShader(QString shaderGuid, Database* dataBase, QString tempPath)
	{
		QJsonObject matDef;
		writeMaterial(matDef, shaderGuid, dataBase);

		auto assetData = dataBase->fetchAssetData(shaderGuid);
        QJsonObject obj = QJsonDocument::fromJson(assetData).object();
		auto graphObj = MaterialHelper::extractNodeGraphFromMaterialDefinition(obj);

		QJsonDocument saveDoc;
		//saveDoc.setObject(materialDef);
		saveDoc.setObject(matDef);

		QString fileName = IrisUtils::join(
			tempPath,
			IrisUtils::buildFileName(matDef["name"].toString(), "material")
		);

		QFile file(fileName);
		file.open(QFile::WriteOnly);
		file.write(saveDoc.toJson());
		file.close();

		// WRITE TO DATABASE
		const QString assetGuid = GUIDManager::generateGUID();
        QByteArray binaryMat = QJsonDocument(matDef).toJson();
		dataBase->createAssetEntry(
			assetGuid,
			QFileInfo(fileName).fileName(),
			static_cast<int>(ModelTypes::Material),
			QString(),
			QString(),
			QString(),
			QByteArray(),
			QByteArray(),
			QByteArray(),
			binaryMat,
			AssetViewFilter::Effects
		);

		//updateMaterialThumbnail(guid, assetGuid);

		MaterialReader reader;
		auto material = reader.parseMaterial(matDef, dataBase);

		// Actually create the material and add shader as it's dependency
		dataBase->createDependency(
			static_cast<int>(ModelTypes::Material),
			static_cast<int>(ModelTypes::Shader),
			assetGuid, shaderGuid,
			Globals::project->getProjectGuid());

		// Add all its textures as dependencies too
		auto values = matDef["values"].toObject();
		for (const auto& prop : graphObj->properties) {
			if (prop->type == PropertyType::Texture) {
				if (!values.value(prop->name).toString().isEmpty()) {

					dataBase->createDependency(
						static_cast<int>(ModelTypes::Material),
						static_cast<int>(ModelTypes::Texture),
						assetGuid, values.value(prop->name).toString(),
						Globals::project->getProjectGuid()
					);

				}
			}
		}

		auto assetMat = new AssetMaterial;
		assetMat->assetGuid = assetGuid;
		assetMat->setValue(QVariant::fromValue(material));

		// write material guid to graph and save graph
		graphObj->materialGuid = assetGuid;
		QJsonDocument doc;
		auto graphObject = MaterialHelper::serialize(graphObj);
		doc.setObject(graphObject);
        dataBase->updateAssetAsset(shaderGuid, doc.toJson());

		return assetGuid;
	}



	// utils
	static void writeMaterial(QJsonObject& matObj, QString guid, Database* dataBase)
	{
		auto name = dataBase->fetchAsset(guid).name;
		matObj["name"] = name;
		matObj["version"] = 2.0;
		matObj["shaderGuid"] = guid;
		matObj["values"] = writeMaterialValuesFromShader(dataBase,guid);
	}

	static QJsonObject writeMaterialValuesFromShader(Database* db, QString guid)
	{
        QJsonObject obj = QJsonDocument::fromJson(db->fetchAssetData(guid)).object();
		auto graphObj = MaterialHelper::extractNodeGraphFromMaterialDefinition(obj);
		QJsonObject valuesObj;
		for (auto prop : graphObj->properties) {
			if (prop->type == PropertyType::Bool) {
				valuesObj[prop->name] = prop->getValue().toBool();
			}

			if (prop->type == PropertyType::Float) {
				valuesObj[prop->name] = prop->getValue().toFloat();
			}

			if (prop->type == PropertyType::Color) {
				valuesObj[prop->name] = prop->getValue().value<QColor>().name();
			}

			if (prop->type == PropertyType::Texture) {
				auto id = prop->getValue().toString();
				valuesObj[prop->name] = id;
			}

			if (prop->type == PropertyType::Vec2) {
				valuesObj[prop->name] = SceneWriter::jsonVector2(prop->getValue().value<QVector2D>());
			}

			if (prop->type == PropertyType::Vec3) {
				valuesObj[prop->name] = SceneWriter::jsonVector3(prop->getValue().value<QVector3D>());
			}

			if (prop->type == PropertyType::Vec4) {
				valuesObj[prop->name] = SceneWriter::jsonVector4(prop->getValue().value<QVector4D>());
			}
		}

		return valuesObj;
	}

	static QString getAssetPath(const AssetRecord& asset)
	{
		QString path;
		if (asset.view_filter == AssetViewFilter::Editor) {
            path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + Constants::PROJECT_FOLDER;
			//auto projectFolder = SettingsManager::getDefaultManager()->getValue("default_directory", spath).toString();
		}
		else if (asset.view_filter == AssetViewFilter::AssetsView ||	
				 asset.view_filter == AssetViewFilter::Effects) {
			auto assetPath = IrisUtils::join(
                QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
				"AssetStore"
			);
			path = QDir(assetPath).filePath(asset.guid);
		}
		
		return IrisUtils::join(path, asset.name);
	}
};

#endif //SHADERGRAPH_EXPORTER
