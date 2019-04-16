#include <fstream>
#include "Simbody.h"
#include "Exception.h"
#include "FileAdapter.h"
#include "TimeSeriesTable.h"
#include "APDMDataReader.h"

namespace OpenSim {

const std::vector<std::string> APDMDataReader::acceleration_labels{
        "/Acceleration/X", "/Acceleration/Y", "/Acceleration/Z"
}; 
const std::vector<std::string> APDMDataReader::angular_velocity_labels{
    "/Angular Velocity/X", "/Angular Velocity/Y","/Angular Velocity/Z"
};
const std::vector<std::string> APDMDataReader::magnetic_heading_labels{
    "/Magnetic Field/X", "/Magnetic Field/Y","/Magnetic Field/Z"
};
const std::vector<std::string> APDMDataReader::orientation_labels{
    "/Orientation/Scalar", "/Orientation/X", "/Orientation/Y","/Orientation/Z"
};

const std::string APDMDataReader::TimeLabel{ "Time" };

APDMDataReader* APDMDataReader::clone() const {
    return new APDMDataReader{*this};
}

DataAdapter::OutputTables 
APDMDataReader::extendRead(const std::string& fileName) const {

    OPENSIM_THROW_IF(fileName.empty(),
        EmptyFileName);

    std::ifstream in_stream{ fileName };
    OPENSIM_THROW_IF(!in_stream.good(),
        FileDoesNotExist,
        fileName);

    OPENSIM_THROW_IF(in_stream.peek() == std::ifstream::traits_type::eof(),
        FileIsEmpty,
        fileName);

    std::vector<std::string> labels;
    
    double dataRate = SimTK::NaN;
    std::vector<int> accIndex;
    std::vector<int>  gyroIndex;
    std::vector<int>  magIndex;
    std::vector<int>  orientationsIndex;

    int n_imus = _settings.getProperty_ExperimentalSensors().size();
    int last_size = 1024;
    // Will read data into pre-allocated Matrices in-memory rather than appendRow
    // on the fly which copies the whole table on every call.
    SimTK::Matrix_<SimTK::Quaternion> rotationsData{ last_size, n_imus };
    SimTK::Matrix_<SimTK::Vec3> linearAccelerationData{ last_size, n_imus };
    SimTK::Matrix_<SimTK::Vec3> magneticHeadingData{ last_size, n_imus };
    SimTK::Matrix_<SimTK::Vec3> angularVelocityData{ last_size, n_imus };
    std::vector<double> times;
    times.resize(last_size);
    // Format looks like this:
    // Header Line 1: Test Name:, $String,,,,,..
    // Header Line 2: Sample Rate:, $Value, Hz,,,,,
    // Labels Line 3: Time {SensorName/Acceleration/X,SensorName/Acceleration/Y,SensorName/Acceleration/Z,....} repeated per sensor
    // Units Line 4: s,{m/s^2,m/s^2,m/s^2....} repeated 
    int header_lines = 4;
    std::string line;
    // Line 1
    std::getline(in_stream, line);
    std::vector<std::string> tokens = FileAdapter::tokenize(line, ",");
    std::string trialName = tokens[1]; // May contain spaces
    // Line 2
    std::getline(in_stream, line);
    tokens = FileAdapter::tokenize(line, ",");
    dataRate = std::stod(tokens[1]);
    // Line 3, find columns for IMUs
    std::getline(in_stream, line);
    tokens = FileAdapter::tokenize(line, ",");
    OPENSIM_THROW_IF((tokens[0] != TimeLabel), UnexpectedColumnLabel,
        fileName,
        TimeLabel,
        tokens[0]);

    for (int imu_index = 0; imu_index < n_imus; ++imu_index) {
        std::string sensorName = _settings.get_ExperimentalSensors(imu_index).getName();
        labels.push_back(_settings.get_ExperimentalSensors(imu_index).get_name_in_model());
        find_start_column(tokens, APDMDataReader::acceleration_labels, sensorName, accIndex);
        find_start_column(tokens, APDMDataReader::angular_velocity_labels, sensorName, gyroIndex);
        find_start_column(tokens, APDMDataReader::magnetic_heading_labels, sensorName, magIndex);
        find_start_column(tokens, APDMDataReader::orientation_labels, sensorName, orientationsIndex);
    }
    // Will create a table to map 
    // internally keep track of what data was found in input files
    bool foundLinearAccelerationData = accIndex.size()>0;
    bool foundMagneticHeadingData = magIndex.size()>0;
    bool foundAngularVelocityData = gyroIndex.size()>0;

    // If no Orientation data is available or dataRate can't be deduced we'll abort completely
    OPENSIM_THROW_IF((orientationsIndex.size() == 0 || SimTK::isNaN(dataRate)),
        TableMissingHeader);
    // Line 4, Units unused
    std::getline(in_stream, line);

    // For all tables, will create row, stitch values from different sensors then append
    bool done = false;
    double time = 0.0;
    double timeIncrement = 1 / dataRate;
    int rowNumber = 0;
    while (!done){
        // Make vectors one per table
        TimeSeriesTableQuaternion::RowVector
            orientation_row_vector{ n_imus, SimTK::Quaternion() };
        TimeSeriesTableVec3::RowVector
            accel_row_vector{ n_imus, SimTK::Vec3(SimTK::NaN) };
        TimeSeriesTableVec3::RowVector
            magneto_row_vector{ n_imus, SimTK::Vec3(SimTK::NaN) };
        TimeSeriesTableVec3::RowVector
            gyro_row_vector{ n_imus, SimTK::Vec3(SimTK::NaN) };
        std::vector<std::string> nextRow = FileAdapter::getNextLine(in_stream, ",");
        if (nextRow.empty()) {
            done = true;
            break;
        }
        // Cycle through the imus collating values
        for (int imu_index = 0; imu_index < n_imus; ++imu_index) {
            // parse gyro info from in_stream
           if (foundLinearAccelerationData)
                accel_row_vector[imu_index] = SimTK::Vec3(std::stod(nextRow[accIndex[imu_index]]),
                    std::stod(nextRow[accIndex[imu_index] + 1]), std::stod(nextRow[accIndex[imu_index] + 2]));
            if (foundMagneticHeadingData)
                magneto_row_vector[imu_index] = SimTK::Vec3(std::stod(nextRow[magIndex[imu_index]]),
                    std::stod(nextRow[magIndex[imu_index] + 1]), std::stod(nextRow[magIndex[imu_index] + 2]));
            if (foundAngularVelocityData)
                gyro_row_vector[imu_index] = SimTK::Vec3(std::stod(nextRow[gyroIndex[imu_index]]),
                    std::stod(nextRow[gyroIndex[imu_index] + 1]), std::stod(nextRow[gyroIndex[imu_index] + 2]));
            // Create Quaternion from values in file, assume order in file W, X, Y, Z
            orientation_row_vector[imu_index] = 
                SimTK::Quaternion(std::stod(nextRow[orientationsIndex[imu_index]]),
                    std::stod(nextRow[orientationsIndex[imu_index] + 1]),
                    std::stod(nextRow[orientationsIndex[imu_index] + 2]),
                    std::stod(nextRow[orientationsIndex[imu_index] + 3]));
        }
        // append to the tables
        times[rowNumber] = time;
        if (foundLinearAccelerationData) 
            linearAccelerationData[rowNumber] =  accel_row_vector;
        if (foundMagneticHeadingData) 
            magneticHeadingData[rowNumber] = magneto_row_vector;
        if (foundAngularVelocityData) 
            angularVelocityData[rowNumber] = gyro_row_vector;
        rotationsData[rowNumber] = orientation_row_vector;
        // We could get some indication of time from file or generate time based on rate
        // Here we use the latter mechanism.
        time += timeIncrement;
        rowNumber++;
        if (std::remainder(rowNumber, last_size) == 0) {
            // resize all Data/Matrices, double the size  while keeping data
            int newSize = last_size*2;
            times.resize(newSize);
            // Repeat for Data matrices in use
            if (foundLinearAccelerationData) linearAccelerationData.resizeKeep(newSize, n_imus);
            if (foundMagneticHeadingData) magneticHeadingData.resizeKeep(newSize, n_imus);
            if (foundAngularVelocityData) angularVelocityData.resizeKeep(newSize, n_imus);
            rotationsData.resizeKeep(newSize, n_imus);
            last_size = newSize;
        }
    }
    // Trim Matrices in use to actual data and move into tables
    times.resize(rowNumber);
    // Repeat for Data matrices in use and create Tables from them or size 0 for empty
    linearAccelerationData.resizeKeep(foundLinearAccelerationData? rowNumber : 0,
        n_imus);
    magneticHeadingData.resizeKeep(foundMagneticHeadingData? rowNumber : 0,
            n_imus);
    angularVelocityData.resizeKeep(foundAngularVelocityData? rowNumber :0,
        n_imus);
    rotationsData.resizeKeep(rowNumber, n_imus);
    // Now create the tables from matrices
    // Create 4 tables for Rotations, LinearAccelerations, AngularVelocity, MagneticHeading
    // Tables could be empty if data is not present in file(s)
    DataAdapter::OutputTables tables{};
    auto orientationTable = std::make_shared<TimeSeriesTableQuaternion>(times, rotationsData, labels);
    orientationTable->updTableMetaData()
        .setValueForKey("DataRate", std::to_string(dataRate));
    tables.emplace(IMUDataUtilities::Orientations, orientationTable);

    std::vector<double> emptyTimes;
    auto accelerationTable = (foundLinearAccelerationData ?
        std::make_shared<TimeSeriesTableVec3>(times, linearAccelerationData, labels) :
        std::make_shared<TimeSeriesTableVec3>(emptyTimes, linearAccelerationData, labels));
    accelerationTable->updTableMetaData()
        .setValueForKey("DataRate", std::to_string(dataRate));
    tables.emplace(IMUDataUtilities::LinearAccelerations, accelerationTable);

    auto magneticHeadingTable = (foundMagneticHeadingData ?
        std::make_shared<TimeSeriesTableVec3>(times, magneticHeadingData, labels) :
        std::make_shared<TimeSeriesTableVec3>(emptyTimes, magneticHeadingData, labels));
    magneticHeadingTable->updTableMetaData()
        .setValueForKey("DataRate", std::to_string(dataRate));
    tables.emplace(IMUDataUtilities::MagneticHeading, magneticHeadingTable);

    auto angularVelocityTable = (foundAngularVelocityData ?
        std::make_shared<TimeSeriesTableVec3>(times, angularVelocityData, labels) :
        std::make_shared<TimeSeriesTableVec3>(emptyTimes, angularVelocityData, labels));
    angularVelocityTable->updTableMetaData()
        .setValueForKey("DataRate", std::to_string(dataRate));
    tables.emplace(IMUDataUtilities::AngularVelocity, angularVelocityTable);

    return tables;
}

void APDMDataReader::find_start_column(std::vector<std::string> tokens, 
    std::vector<std::string> search_labels,
    const std::string& sensorName,
    std::vector<int>& indices) const {
    // Search for "sensorName/{search_labels} in tokens, append result to indices if found"
    std::string firstLabel = sensorName + search_labels[0];
    // look for first label, when found check/confirm the rest. Out of order is not supported
    int found_index = -1;
    std::vector<std::string>::iterator it = std::find(tokens.begin(), tokens.end(), firstLabel);
    if (it != tokens.end()) {
        found_index = static_cast<int>(std::distance(tokens.begin(), it));
        // now check the following indices for match with remaining search_labels 
        bool match = true;
        for (int remaining = 1; remaining < search_labels.size() && match; remaining++) {
            match = tokens[found_index + remaining].
                compare(sensorName + search_labels[remaining]) == 0;
        }
        if (match) {
            indices.push_back(found_index);
                return;
            }
            else { // first label found but the remaining didn't. Warn
                return;
            }
        }
    return; // not found
    }
}

