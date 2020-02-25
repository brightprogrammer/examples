/**
 * An example of using Feed Forward Neural Network (FFN) for
 * solving Digit Recognizer problem from Kaggle website.
 *
 * The full description of a problem as well as datasets for training
 * and testing are available here https://www.kaggle.com/c/digit-recognizer
 *
 * mlpack is free software; you may redistribute it and/or modify it under the
 * terms of the 3-clause BSD license.  You should have received a copy of the
 * 3-clause BSD license along with mlpack.  If not, see
 * http://www.opensource.org/licenses/BSD-3-Clause for more information.
 *
 * @author Eugene Freyman
 */

#include <mlpack/prereqs.hpp>
#include <mlpack/core.hpp>
#include <mlpack/core/data/split_data.hpp>
#include <mlpack/core/util/cli.hpp>
#include <mlpack/core/util/mlpack_main.hpp>

#include <mlpack/methods/ann/layer/layer.hpp>
#include <mlpack/methods/ann/ffn.hpp>
#include <mlpack/methods/ann/init_rules/glorot_init.hpp>

#include "kaggle_utils.hpp"

#include <ensmallen.hpp>
#include <ensmallen_bits/callbacks/callbacks.hpp>

using namespace mlpack;
using namespace mlpack::ann;

using namespace arma;
using namespace std;
using namespace mlpack::util;
using namespace ens;

PROGRAM_INFO(
  "DigitRecognizer",
  "This software is part of mlpack model zoo, it implements digits recognizer "
  "which classify hand written images based on the MNIST data set.",
  "This software can be used in Kaggle DigitRecognizer competition, to obtain "
  "the training and testing set, You can visit this website: "
  "https://www.kaggle.com/c/digit-recognizer/data",
  SEE_ALSO("DigitRecognizer competition website",
           "https://www.kaggle.com/c/digit-recognizer"),
  SEE_ALSO("MNIST Dataset website", "http://yann.lecun.com/exdb/mnist/"));

// // Training dataset parameters.
PARAM_STRING_IN("training_dataset",
                "Full path to the file containing the training set.",
                "t",
                "../Kaggle/data/train.csv");

// Testing/classification parameters.
PARAM_STRING_IN("testing_datatest",
                "Full path to the file containing the test set.",
                "l",
                "../Kaggle/data/test.csv");

// Prediction results parameters.
PARAM_STRING_IN("prediction_result",
                "File name in which prediction will be saved.",
                "P",
                "../Kaggle/results.csv");

// // Model loading/saving.
// PARAM_MODEL_IN("input_model", "Full path to the file which has input model.",
// "m"); PARAM_MODEL_OUT("output_model", "Full path to the file in which output
// model will be saved.", "M");

// Division ration between Train set and validation set, default 10%.
PARAM_DOUBLE_IN("Ratio",
                "Percentage of validation set from the training set.",
                "r",
                0.1);

PARAM_INT_IN(
  "Batch_size",
  "Number of data points to be trained in each iteration of the optimizer.",
  "b",
  64);

static void mlpackMain()
{
  // Dataset is randomly split into validation
  // and training parts in the following ratio.
  double RATIO = 0.1;  
  if (CLI::HasParam("Ratio")) 
  {
    RATIO = CLI::GetParam<double>("Ratio");
  }
  
  // The number of neurons in the first layer.
  constexpr int H1 = 200;
  // The number of neurons in the second layer.
  constexpr int H2 = 100;

  // The solution is done in several approaches (CYCLES), so each approach
  // uses previous results as a starting point and has different optimizer
  // options (here the step size is different).

  // Step size of the optimizer.
  constexpr double STEP_SIZE = 5e-3;

  // Number of data points in each iteration of SGD
  size_t BATCH_SIZE = 64;
  if (CLI::HasParam("Batch_size")) 
  {
    BATCH_SIZE = (size_t) CLI::GetParam<int>("Batch_size");
  }

  // Labeled dataset that contains data for training is loaded from CSV file,
  // rows represent features, columns represent data points.
  mat tempDataset;
  // The original file could be download from
  // https://www.kaggle.com/c/digit-recognizer/data

  if (CLI::HasParam("training_dataset"))
  {
    Log::Info << "Reading dataset from:"
    << CLI::GetPrintableParam<std::string>("training_dataset");
    data::Load(std::move(CLI::GetParam<std::string>("training_dataset")), 
    tempDataset, true);
  }
  else
  {
    data::Load("../Kaggle/data/train.csv", tempDataset, true);
  }

  // Originally on Kaggle dataset CSV file has header, so it's necessary to
  // get rid of the this row, in Armadillo representation it's the first column.
  mat dataset = tempDataset.submat(0, 1,
    tempDataset.n_rows - 1, tempDataset.n_cols - 1);

  // Splitting the dataset on training and validation parts.
  mat train, valid;
  data::Split(dataset, train, valid, RATIO);

  // Getting training and validating dataset with features only and then
  // normalising
  const mat trainX = train.submat(1, 0, train.n_rows - 1,
      train.n_cols - 1) / 255.0;
  const mat validX = valid.submat(1, 0, valid.n_rows - 1,
      valid.n_cols - 1) / 255.0;

  // Allow infinite number of iterations utill we stopped by EarlyStopAtMinLoss.
  const int ITERATIONS_PER_CYCLE = 0;
  // According to NegativeLogLikelihood output layer of NN, labels should
  // specify class of a data point and be in the interval from 1 to
  // number of classes (in this case from 1 to 10).

  // Creating labels for training and validating dataset.
  const mat trainY = train.row(0) + 1;
  const mat validY = valid.row(0) + 1;

  // Specifying the NN model. NegativeLogLikelihood is the output layer that
  // is used for classification problem. GlorotInitialization means that
  // initial weights in neurons are a uniform gaussian distribution
  FFN<NegativeLogLikelihood<>, GlorotInitialization> model;
  // This is intermediate layer that is needed for connection between input
  // data and relu layer. Parameters specify the number of input features
  // and number of neurons in the next layer.
  model.Add<Linear<> >(trainX.n_rows, H1);
  // The first relu layer.
  model.Add<ReLULayer<> >();
  // Intermediate layer between relu layers.
  model.Add<Linear<> >(H1, H2);
  // The second relu layer.
  model.Add<ReLULayer<> >();
  // Dropout layer for regularization. First parameter is the probability of
  // setting a specific value to 0.
  model.Add<Dropout<> >(0.2);
  // Intermediate layer.
  model.Add<Linear<> >(H2, 10);
  // LogSoftMax layer is used together with NegativeLogLikelihood for mapping
  // output values to log of probabilities of being a specific class.
  model.Add<LogSoftMax<> >();

  Log::Info << "Training ...";

  // Setting parameters Stochastic Gradient Descent (SGD) optimizer.
  SGD<AdamUpdate> optimizer(
    // Step size of the optimizer.
    STEP_SIZE,
    // Batch size. Number of data points that are used in each iteration.
    BATCH_SIZE,
    // Max number of iterations
    ITERATIONS_PER_CYCLE,
    // Tolerance, used as a stopping condition is set to -1.
    // This value means we never stop by this condition and continue to optimize
    // until we stopped by EarlyStopAtMinLoss.
    -1,
    // Shuffle. If optimizer should take random data points from the dataset at
    // each iteration.
    true,
    // Adam update policy.
    AdamUpdate(1e-8, 0.9, 0.999));

  // Don't reset optimizer's parameters between cycles.
  optimizer.ResetPolicy() = false;

  // Train neural network. If this is the first iteration, weights are
  // random, using current values as starting point otherwise.
  model.Train(trainX, 
              trainY,
              optimizer,
              ens::PrintLoss(),
              ens::ProgressBar(),
              ens::EarlyStopAtMinLoss(),
              ens::StoreBestCoordinates<arma::mat>());

  mat predout;
  // Getting predictions on training data points.
  model.Predict(trainX, predout);
  // Calculating accuracy on training data points.
  Row<size_t> predlabels = getLabels(predout);
  double trainaccuracy = accuracy(predlabels, trainY);
  // Getting predictions on validating data points.
  model.Predict(validX, predout);
  // Calculating accuracy on validating data points.
  predlabels = getLabels(predout);
  double validaccuracy = accuracy(predlabels, validY);

  Log::Info << "Accuracy: train = "<< trainaccuracy << "%," <<
  " valid = "<< validaccuracy << "%" << endl;
  
  Log::Info << "predicting ..." << endl;

  // Loading test dataset (the one whose predicted labels
  // should be sent to kaggle website).
  // As before, it's necessary to get rid of header.

  // The original file could be download from
  // https://www.kaggle.com/c/digit-recognizer/data
  data::Load("../Kaggle/data/test.csv", tempDataset, true);
  mat testX = tempDataset.submat(0, 1,
    tempDataset.n_rows - 1, tempDataset.n_cols - 1);

  mat testPredOut;
  // Getting predictions on test data points.
  model.Predict(testX, testPredOut);
  // Generating labels for the test dataset.
  Row<size_t> testPred = getLabels(testPredOut);
  Log::Info << "Saving predicted labels to \"Kaggle/results.csv\" ..." << endl;

  // Saving results into Kaggle compatibe CSV file.
  if (CLI::HasParam("prediction_result"))
  {
    save(std::move(CLI::GetParam<std::string>("prediction_result")),
    "ImageId,Label", testPred);
  }
  else 
  {
   save("Kaggle/results.csv", "ImageId,Label", testPred);
  }

  Log::Info << "Results were saved to \"results.csv\" and could be uploaded to "
       << "https://www.kaggle.com/c/digit-recognizer/submissions for a competition";
  Log::Info << "Finished";
}
