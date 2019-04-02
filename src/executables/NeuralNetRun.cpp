//
// Created by Laivins, Josiah on 2019-03-16.
//

#include <iostream>
#include "../nn/NeuralNet.h"
#include "NeuralNetRun.h"
#include "../utils/DatasetCar.h"
#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/videoio.hpp"
#include "../utils/BoardWriter.h"

namespace nn {

    cv::Mat getImage(DatasetCar& dataset, int index) {
        cv::Mat image = imread(dataset[index].filename);
        cv::Mat imageNormalized;
        image.convertTo(imageNormalized, CV_32F, 1.0 / 255, 0);
        cv::resize(imageNormalized, imageNormalized, cv::Size(64, 64));
        return imageNormalized.reshape(1,1);
    }

    float getNormalY(DatasetCar& dataset, int index) {
        float y = (dataset[index].steeringAngle - dataset.minFeatureValue) /
                   (dataset.maxFeatureValue - dataset.minFeatureValue);
        if (dataset.maxFeatureValue - dataset.minFeatureValue == 0) {
            cout << "Note, y is being set to 0" << endl;
            y = 0;
        }

        return (float)y;
    }

    cv::Mat getBinnedY(float y, unsigned long bins) {
        // Convert y to a vector dtype
        long closestBin = lround(y * bins) - 1;
        std::vector<float> preHist(bins, 0);
        preHist[closestBin] = 1;
        cv::Mat histogram(1, static_cast<int>(preHist.size()), CV_32F);
        for(int i=0; i<histogram.cols; ++i)
            histogram.at<float>(0, i) = preHist.at(i);

        return histogram;
    }

    cv::Mat getGuassianBinnedY(cv::Mat y, int smoothSize) {
        cv::Mat smoothedY;
        GaussianBlur(y, smoothedY, cv::Size(0, 0), smoothSize, 0);

        return smoothedY;
    }

    float getAccuracy(cv::Mat predY, cv::Mat y, DatasetCar validationDataset, int bins, float tolarance=5) {
        vector<int> predictedLocations;
        vector<int> actualLocations;

        // Get the max locations
        double min, max;
        cv::Point predMinLoc, predMaxLoc;
        cv::Point minLoc, maxLoc;

        // Iterate through each row and get the location of the max
        for(int i = 0; i < predY.rows; i++) {
            cv::minMaxLoc(predY.row(i), &min, &max, &predMinLoc, &predMaxLoc);
            cv::minMaxLoc(y.row(i), &min, &max, &minLoc, &maxLoc);
            predictedLocations.push_back(predMaxLoc.x);
            actualLocations.push_back(maxLoc.x);
        }
        // Convert each to their actual angles
        vector<double> predictedAngles;
        vector<double> actualAngles;
        for (long i = 0; i < predictedLocations.size(); i++) {
            float predictedAngle = predictedLocations.at(i) / float(bins);
            float actualAngle = actualLocations.at(i) / float(bins);
            predictedAngles.push_back((predictedAngle * validationDataset.maxFeatureValue) - validationDataset.minFeatureValue);
            actualAngles.push_back((actualAngle * validationDataset.maxFeatureValue) - validationDataset.minFeatureValue);
        }
        // Determine accuracy of angles within a tolerance
        cv::Mat absOfAngles;
        cv::subtract(predictedAngles, actualAngles, absOfAngles);
        absOfAngles = cv::abs(absOfAngles);
        float acc = 0;

        for (int i = 0; i < absOfAngles.cols; i++) {
            float value = absOfAngles.at<float>(0, i);
            if (value < tolarance) acc++;
        }


        return (acc / absOfAngles.cols) * 100;
    }

    int run(int argc, char *argv[]) {
        printf("Running Neural Net Run");
        // Load dataset
        DatasetCar dataset(-1);
        dataset.readCsv(-1, true);
        // Setup the writer
        BoardWriter w;

        vector<DatasetCar> d = dataset.split(.7);
        DatasetCar trainDataset = d[0];
        DatasetCar validationDataset = d[1];

        // Create neural net
        NeuralNet nn = NeuralNet();
        nn.addLayer(64 * 64 * 3, 30, 0, "middle");
        nn.addLayer(30, 120, 0, "middle");
        nn.addLayer(120, 1, 0, "middle");

        // Init the validation variables
        cv::Mat validationX;
        cv::Mat validationY;
        for (int i = 0; i < validationDataset.getSize(); i++) {
            validationX.push_back(getImage(validationDataset, i));
            validationY.push_back(getBinnedY(getNormalY(validationDataset, i), 120));
        }

        std::vector<float> validationAccuracy = vector<float>();
        std::vector<float> trainAccuracy = vector<float>();


        // Define epochs
        int epochs = 40;
        for (int epoch = 0; epoch < epochs; epoch++) {
            printf("Starting epoch %i\n", epoch);

            cv::Mat y;
            cv::Mat predY;

            int batchSize = 30;
            for (int i = 0; i < trainDataset.getSize(); i += batchSize) {
                cv::Mat batchX;
                cv::Mat batchY;
                printf("\tStarting batch %i\n", i);

                for (int imageLoc = i, slot = 0; imageLoc < i + batchSize and imageLoc < trainDataset.getSize();
                     imageLoc++, slot++) {
                    batchX.push_back(getImage(trainDataset, imageLoc));
                    batchY.push_back(getGuassianBinnedY(getBinnedY(getNormalY(trainDataset, imageLoc), 120), 5));
                }

                y.push_back(batchY);
                predY.push_back(nn.forward(batchX));

                nn.train(batchX, batchY, 30, batchSize);
            }
            // Eval the RMSE independent of batches
            nn.logBatchRMSE(predY, y);
            cv::Mat validationPredY = nn.forward(validationX);
            nn.logBatchRMSEValidation(validationPredY, validationY);
            validationAccuracy.push_back(getAccuracy(validationPredY, validationY, validationDataset, 120));
            trainAccuracy.push_back(getAccuracy(predY, y, trainDataset, 120));


            w.write("RMSE", nn.rmse.back(), 0);
            w.write("Validation RMSE", nn.rmseValidate.back(), 0);
            w.write("Validation Accuracy Percent", validationAccuracy.back(), 0);
            w.write("Train Accuracy Percent", trainAccuracy.back(), 0);

            printf("Epoch %i RMSE: %f Validation RMSE: %f\n Validation Accuracy is %f percent\n Train Accuracy is %f percent\n",
                    epoch, nn.rmse.back(), nn.rmseValidate.back(), validationAccuracy.back(), trainAccuracy.back());
        }
        return 0;
    }
}