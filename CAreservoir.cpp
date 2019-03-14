#include "stdafx.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <vector>
#include <time.h>
#include <string>
#include <iomanip>
#include "dataanalysis.h"
#include "CAreservoir.h"
#include <omp.h>
#include <iostream>
#include <fstream>

using namespace alglib;
using namespace std;

void parallel_5_bit();
void parallel_SVM(int num_tests, int cores, string rule_file);
void usage();

int main(int argc, char **argv) {
    int arg_index = 1;
    int max_arg = argc - 2;
    bool error = true;
    bool draw = false;
    bool parallel = false;
    int runs, cores;

    if (argc < 3) 
	usage();
    while (arg_index < max_arg) {
	if (!strcmp(argv[arg_index], "-d")) {
	    draw = true;
	    error = false;
	}
	else if (!strcmp(argv[arg_index], "-p")) {
	    parallel = true;
            runs = atoi(argv[++arg_index]);
	    cores = atoi(argv[++arg_index]);
	    error = false;
	}
	++arg_index;
	if (error)
	    usage();
    }
    STATES = atoi(argv[arg_index]);
    RULELENGTH = pow(STATES, NEIGHBORHOOD);
    ++arg_index;
    string rule_file(argv[arg_index]);
        
    try {
	srand(time(NULL));
	if (parallel)
	    parallel_SVM(runs, cores, rule_file);
	else {
	    CA ca;
	    ca.load_rule(rule_file);
	    real_2d_array training_data;
	    vector<linearmodel> output(3);
	    //ca.set_rule(RULE3_3);
	    // Add one for target
	    training_data.setlength(SEQUENCE_LENGTH * TEST_SETS, READOUT_LENGTH + 1);
	    cout << "Building training data\n";
	    ca.train_5_bit(training_data);
	    //ca.build_5_bit_model(training_data, output);
	    ca.set_5_bit_targets();
	    ca.build_SVM_model(training_data);
	    if (draw)
		ca.save_CA(training_data);
	}
    }
    catch(IncorrectRuleLengthException e)
    {
        cout << "Error: rule length does not match number of states and neighborhood.\n";
    }	    
    return 0;
}

/***************************************************************************************/

void usage() {
    cout << "Usage: CAreservoir [options] <# of states> <rule file>\n";
    cout << "Options:\n";
    cout << "-d 		  -> save CA to ca.txt and draw in ca.ppm\n";
    cout << "-p <int1> <int2> -> parallel: <int1> runs on up to <int2> cores\n"; 
    exit(0);
}

/***************************************************************************************/

void parallel_SVM(int num_tests, int cores, string rule_file) {
    int success = 0;
    //omp_set_nested(1);
    srand(time(NULL));
    // Don't exceed number of cores
    omp_set_num_threads(cores);
    #pragma omp parallel
    {
        #pragma omp for nowait
        for (int i = 0; i < num_tests; ++i) 
	{
            //omp_set_num_threads(3);
	    CA ca;
	    real_2d_array training_data;
	    //ca.set_rule(RULE90);
	    ca.load_rule(rule_file);
	    training_data.setlength(SEQUENCE_LENGTH * TEST_SETS, READOUT_LENGTH + 1);

	    cout << "Building training data\n";
	    ca.train_5_bit(training_data);
            ca.set_5_bit_targets();
	    if (ca.build_SVM_model(training_data) == 0) {
                #pragma omp critical
		{
		    ++success;
		}
	    }
	}
    }
    cout << "Successful tests: " << success << ", out of " << num_tests << "." << endl;
}
/***************************************************************************************/

void parallel_5_bit() { 
    int success = 0;
    int num_tests = 100;
    omp_set_nested(1);
    // Don't exceed number of cores
    omp_set_num_threads(16);
    #pragma omp parallel
    {
        #pragma omp for nowait
        for (int i = 0; i < num_tests; ++i) 
	{
            //omp_set_num_threads(3);
	    CA ca;
	    real_2d_array training_data;
	    vector<linearmodel> output(3);
	    ca.set_rule(RULE195);
	    training_data.setlength(SEQUENCE_LENGTH * TEST_SETS, READOUT_LENGTH + 1);

	    cout << "Building training data\n";
	    ca.train_5_bit(training_data);
	    cout << "Building regression models\n";
	    ca.build_5_bit_model(training_data, output);
	    if (ca.test_5_bit(training_data, output) == 0) {
                #pragma omp critical
		{
		    ++success;
		}
	    }
	}
    }
    cout << "Successful tests: " << success << ", out of " << num_tests << "." << endl;
}

/***************************************************************************************/

CA::CA() {
    bool unique;
    int i, j, k;

    _iter = 0;
    _map.resize(R, vector<int>(INPUT_LENGTH));
    _cell.resize(I + 1, vector<int>(WIDTH));
    _rule.resize(RULELENGTH);
    _targets.resize(3, vector<int>(SEQUENCE_LENGTH * TEST_SETS));
    // Initialize first row with 0s
    if (STATES < 3) {
	for (i = 0; i < WIDTH; ++i)
	    _cell[0][i] = 0;
    }
    else {
	for (i = 0; i < WIDTH; ++i)
	    _cell[0][i] = STATES - 1;
    }
    for (i = 0; i < R; ++i) {
	for (j = 0; j < INPUT_LENGTH; ++j) {
	    do {
		unique = true;
                _map[i][j] = rand() % DIFFUSE_LENGTH;
                for (k = 0; k < j; ++k)
		    if (_map[i][j] == _map[i][k])
		      	unique = false;
	    } while (!unique);
	}
    }
}


/***************************************************************************************/

void CA::load_rule(string rule_file) {
    ifstream in;
    char x;

    in.open(rule_file.c_str(), ifstream::in);
    for (int i = 0; i < RULELENGTH; ++i) {
	in >> x;
	if (in.eof())
	    throw IncorrectRuleLengthException();
	_rule[i] = x - 48;
    }
    if (in >> x)
	throw IncorrectRuleLengthException();
}

/***************************************************************************************/

void CA::set_input(vector<int> input) {
    int i, j;

    _iter = 0;
    for (i = 0; i < R; ++i) {
	for (j = 0; j < INPUT_LENGTH; ++j) {
	    // Overwrite initial row with mapped inputs
            _cell[0][i * DIFFUSE_LENGTH + _map[i][j]] = input[j];
	    // Add input state to initial row
            //_cell[0][i * DIFFUSE_LENGTH + _map[i][j]] = 
		//(_cell[0][i * DIFFUSE_LENGTH + _map[i][j]] + input[j]) % STATES;
	}
    }
}

/***************************************************************************************/

void CA::set_rule(vector<int> rule) {
    int i;

    for (i = 0; i < RULELENGTH; ++i)
	_rule[i] = rule[i];
}
	
/***************************************************************************************/

void CA::check_CA(real_2d_array& training_data) {
    int data_index = 0;

    for (int i = 0; i < READOUT_LENGTH; ++i)
	apply_rule(training_data, data_index++);

    save_CA(training_data);
    //draw_CA(training_data);
}


/***************************************************************************************/

void CA::draw_CA(alglib::real_2d_array& training_data) {
    int i, j, k, l;
    char ans;
    int num_colors = 3 * STATES;
    char charState, state;
    int layer[WIDTH];
    do { 
	FILE* f_out = fopen("ca.ppm", "w"); 
	FILE* f_in = fopen("ca.txt", "r");

	fputs("P3\n", f_out);
	// Square PPM image of beginning of training data
	fprintf(f_out, "%d %d\n", 3 * WIDTH, 3 * WIDTH);
	fputs("255\n", f_out);
	vector<int> colors(num_colors);
	// Set colors randomly
	for (i = 0; i < num_colors; ++i)
	    colors[i] = rand() % 256;
        for (i = 0; i < WIDTH; ++i)
        {
       	  for (j = 0; j < WIDTH; ++j)
	  {
	     ans = (int)fscanf(f_in, " %c", &charState);
	     state =  (int) charState - 48;
	     layer[j] = state;
	     // Draw cells as 3 x 3 blocks
	     for (k = 0; k < 3; ++k)
		fprintf(f_out, "%d %d %d ", colors[state*3], colors[state*3+1], colors[state*3+2]);
	     if (i % 3 == 2)
		fprintf(f_out, "\n");
	  }
	  for (j = 0; j < 2; ++j)
	   for (l = 0; l < WIDTH; ++l)
	      for (k = 0; k < 3; ++k)
		 fprintf(f_out, "%d %d %d ", colors[layer[l]*3], colors[layer[l]*3+1], colors[layer[l]*3+2]);
       }
       fclose(f_out);
       fclose(f_in);
       cout << "Redraw CA? (y or n)  ";
       cin >> ans;
    } while (ans == 'y' || ans == 'Y');
}


/***************************************************************************************/

void CA::apply_rule(real_2d_array& training_data, int data_index) {
    int i, j;
    int rule_index, rule_window[NEIGHBORHOOD];
    int start = -(NEIGHBORHOOD / 2);
    int finish = NEIGHBORHOOD / 2;
    int n = RULELENGTH - 1;

    _iter = 0;
    //cout << "Applying rule\n";
    for (; _iter < I; ++_iter) {
	// Could be optimized to only call mod() at ends of row
	for (i = 0; i < WIDTH; ++i) {
	    for (j = start, rule_index = 0; j <= finish; ++j, ++rule_index)
		rule_window[rule_index] = _cell[_iter][mod(i + j, WIDTH)];
	    _cell[_iter + 1][i] = training_data[data_index][i + WIDTH * _iter]
		= _rule[n -  base_N_to_dec(rule_window, STATES, NEIGHBORHOOD)];
	}
    }
    // copy last row to initial position
    for (i = 0; i < WIDTH; ++i)
	_cell[0][i] = _cell[_iter][i];
    //cout << "Data_index: " << data_index << endl;
}


/***************************************************************************************/

void CA::set_5_bit_targets() {
    int data_index, i, test_set, time_step;
    int distractor_end = SEQUENCE_LENGTH - 5;

    data_index = 0;
    for (test_set = 0; test_set < TEST_SETS; ++test_set) {
	for (i = 0; i < distractor_end; ++i) {
	    _targets[0][data_index] = 0;
            _targets[1][data_index] = 0;
            _targets[2][data_index] = 1;
	    ++data_index;
	}
	// Recall period
	for (time_step = 0; i < SEQUENCE_LENGTH; ++i, ++time_step) {
	    _targets[0][data_index] = test_set >> time_step & 1;
            _targets[1][data_index] = 1 - (test_set >> time_step & 1);
	    _targets[2][data_index] = 0;
	    ++data_index;
	}
    }
}

/***************************************************************************************/
   
void CA::call_SVM_functions(int model, int& incorrect, real_2d_array training_data) {
    string build_model = "./SVMTorch  SVM"; 
    string test_results = "./SVMTest -oa SVM_results"; 
    string output_file = "SVM_results";
    string data_file = "SVM";
    int SVMtag;
    int tid = omp_get_thread_num();
    int system_result;
    ofstream out;
    ifstream in;

    out.open((data_file + to_string(tid) +".dat").c_str(), ofstream::out);
    out << SEQUENCE_LENGTH * TEST_SETS << " " << READOUT_LENGTH + 1 << endl;
    // Build input file for SVMTorch
    for (int i = 0; i < SEQUENCE_LENGTH * TEST_SETS; ++i) {
	for (int j = 0; j < READOUT_LENGTH; ++j) {
	    out << training_data[i][j] << " ";
	}
	SVMtag = _targets[model][i] == 1 ? 1 : -1; 
	out << SVMtag << endl;
    }
    // Build and test model
    out.close();
    system_result = system((build_model + to_string(tid) + ".dat SVM_model" + to_string(tid)).c_str());
    puts((build_model + to_string(tid) + ".dat SVM_model" + to_string(tid)).c_str());
    system_result = system((test_results + to_string(tid) + ".dat SVM_model" + 
	    	to_string(tid) + " SVM" + to_string(tid) + ".dat").c_str());
    puts((test_results + to_string(tid) + ".dat SVM_model" +
		to_string(tid) + " SVM" + to_string(tid) + ".dat").c_str());
    in.open((output_file + to_string(tid) + ".dat").c_str(), ifstream::in);
    float result;
    for (int i = 0; i < SEQUENCE_LENGTH * TEST_SETS; ++i) {
	in >> result;
	if ((result < 0 && _targets[model][i] == 1) || (result >= 0 && _targets[model][i] == 0)) { 
	    #pragma omp critical
	    {
		++incorrect;
	    }
	}
    }
    in.close();
}

/***************************************************************************************/

// Nested parallelism wasn't working. Could it cause tid #s to be reused?
int CA::build_SVM_model(real_2d_array& training_data) {
    int incorrect = 0;

//    #pragma omp parallel sections
//    {
//	#pragma omp section
//	{   // model 0
	    call_SVM_functions(0, incorrect, training_data);
//	}
 //      #pragma omp section
//	{  // model 1
	    call_SVM_functions(1, incorrect, training_data);
//	}
//	#pragma omp section
//	{ // model 2
	    call_SVM_functions(2, incorrect, training_data);
//	}
 //   }
    cout << "\nIncorrect: " << incorrect << endl;
    if (incorrect == 0) 
	return 0;
    return 1;
}


/***************************************************************************************/

void CA::build_5_bit_model(real_2d_array& training_data, vector<linearmodel>& output) {
    int time_step1, test_set1, data_index1, data_index2, data_index3;
    int time_step2, test_set2, test_set3;
    int i, j, k, stop = SEQUENCE_LENGTH * TEST_SETS; 
    int distractor_end = SEQUENCE_LENGTH - 5;
    //int model_index;
    ae_int_t info;
    //ae_int_t nvars;   // for lrunpack()
    lrreport rep;

    // Copy data so regressions can be performed in parallel
    real_2d_array training_data2;
    real_2d_array training_data3;
    training_data2.setlength(SEQUENCE_LENGTH * TEST_SETS, READOUT_LENGTH + 1);
    training_data3.setlength(SEQUENCE_LENGTH * TEST_SETS, READOUT_LENGTH + 1);
    for (i = 0; i < stop; ++i) {
	for (j = 0; j < READOUT_LENGTH; ++j) {
	    training_data3[i][j] = training_data2[i][j] = training_data[i][j];
	}
    }

    #pragma omp parallel sections
    {
        #pragma omp section
	{   // model 0	
	    data_index1 = 0;
	    for (test_set1 = 0; test_set1 < TEST_SETS; ++test_set1) {
		for (i = 0; i < distractor_end; ++i) {
		    _targets[0][data_index1] = 0;
		    training_data[data_index1][READOUT_LENGTH] = 0;
		    ++data_index1;
		}
		// Recall period
		for (time_step1 = 0; i < SEQUENCE_LENGTH; ++i, ++time_step1) {
		    training_data[data_index1][READOUT_LENGTH] = 
			test_set1 >> time_step1 & 1;
		    _targets[0][data_index1] = training_data[data_index1][READOUT_LENGTH];
		    ++data_index1;
		}
	    }
	    cout << "Building linear regression model #1\n";
	    lrbuildz(training_data, SEQUENCE_LENGTH * TEST_SETS, READOUT_LENGTH, info,
		    output[0], rep);    // Try lrbuildz()
	}
        #pragma omp section
	{  // model 1
	    data_index2 = 0;
	    for (test_set2 = 0; test_set2 < TEST_SETS; ++test_set2) {
		for (j = 0; j < distractor_end; ++j) {
		    _targets[1][data_index2] = 0;
		    training_data2[data_index2][READOUT_LENGTH] = 0;
		    ++data_index2;
		}
		// Recall period
		for (time_step2 = 0; j < SEQUENCE_LENGTH; ++j, ++time_step2) {
		    training_data2[data_index2][READOUT_LENGTH] = 
			1 - (test_set2 >> time_step2 & 1);
		    _targets[1][data_index2] = training_data2[data_index2][READOUT_LENGTH];
		    ++data_index2;
		}
	    }
	    cout << "Building linear regression model #2\n";
	    lrbuildz(training_data2, SEQUENCE_LENGTH * TEST_SETS, READOUT_LENGTH, info,
		    output[1], rep);    // Try lrbuildz()
	}
        #pragma omp section
	{ // model 2
	    data_index3 = 0;
	    for (test_set3 = 0; test_set3 < TEST_SETS; ++test_set3) {
		for (k = 0; k < distractor_end; ++k) {
		    _targets[2][data_index3] = 1;
		    training_data3[data_index3][READOUT_LENGTH] = 1;
		    ++data_index3;
		}
		// Recall period
		for (; k < SEQUENCE_LENGTH; ++k) {
		    _targets[2][data_index3] = 0;
		    training_data3[data_index3][READOUT_LENGTH] = 0;
		    ++data_index3;
		}
	    }
	    cout << "Building linear regression model #3\n";
	    lrbuildz(training_data3, SEQUENCE_LENGTH * TEST_SETS, READOUT_LENGTH, info,
		    output[2], rep);    // Try lrbuildz()
	}
    }


    /*for (i = 0; i < 3; ++i) {     // Print out coefficients
	real_1d_array coeffs;
	lrunpack(output[i], coeffs, nvars);
	printf("Coefficients: %s\n", coeffs.tostring(4).c_str());
    }*/

    // Print out targets
    /*cout << "Output\n";
    for (i = 0; i < 32; ++i) {
	cout << "Test set # " << i << endl;
	for (int j = 0; j < 5; ++j) {
	    cout << "\t" << _targets[0][i*210 +  205+j] << " " <<_targets[1][i*210 +  205+j] <<
                " " << _targets[2][i*210 +  205+j] << endl;
	}
    }*/



}

/***************************************************************************************/
 
int CA::test_5_bit(real_2d_array& training_data, vector<linearmodel>& output) {
    real_1d_array model_input;
    int incorrect_predictions = 0;
    int model_index = 0;
    int training_data_index, test_set, sequence_index, i;
    double result;
    int result_state;

    cout << setprecision(4);
    model_input.setlength(READOUT_LENGTH);
    for (model_index = 0; model_index < 3; ++model_index) {
	training_data_index = 0;
        for (test_set = 0; test_set < TEST_SETS; ++test_set) {
	    for (sequence_index = 0; sequence_index < SEQUENCE_LENGTH; ++sequence_index) {
		// Copy reservoir sequence into model_input
		for (i = 0; i < READOUT_LENGTH; ++i)
		    model_input[i] = training_data[training_data_index][i];
		result = lrprocess(output[model_index], model_input);
		// !!! This has to be adjusted for different #s of states !!!
		result_state = result < .5 ? 0 : 1;
                if (result_state != _targets[model_index][training_data_index]) {
		    ++incorrect_predictions;
		    cout << "Model: " << model_index+1 << "\tTest Set: " 
			<< test_set << "\tSequence #: " << sequence_index + 1 << 
			"\tCalcuated: " << result << "\tTarget: " <<
			 _targets[model_index][training_data_index] << endl;
		}
		//else cout << "1    ";
		//cout << result << "\t\t" << training_data[training_data_index][READOUT_LENGTH]
		//    << endl;
		//cout << result << "\t\t" << _targets[model_index][training_data_index] << endl;
		++training_data_index;
	    }
	}
    }
    cout << endl << incorrect_predictions << " incorrect predictions.\n";
    return incorrect_predictions;
}

/***************************************************************************************/

void CA::save_CA(real_2d_array& training_data) {
    int i, j;
    int height = SEQUENCE_LENGTH * TEST_SETS;
    int width = READOUT_LENGTH;
    FILE* f_out = fopen("ca.txt", "w");
    //string syst = "./draw2 ca.txt ca2.ppm";;
    
    for (i = 0; i < height; ++i) {
	for (j = 0; j < width; ++j)
	    fprintf(f_out, "%d", (int)training_data[i][j]);
    }
    fclose(f_out);

    //system(syst.c_str());
    //puts(syst.c_str());
    draw_CA(training_data);
}

/***************************************************************************************/

//training_data.setlength(SEQUENCE_LENGTH * TEST_SETS, READOUT_LENGTH + 1);
void CA::train_5_bit(real_2d_array& training_data) {
    int time_step ,test_set;
    int data_index = 0;
    int distractor_end = SEQUENCE_LENGTH - 6;
    vector<int> input(4);

    //cout << "Input\n";
    for (test_set = 0; test_set < TEST_SETS; ++test_set) {
	//cout << "Test Set: " << test_set << endl;
	// Input signal
	for (time_step = 0; time_step < 5; ++time_step) {
	    input[0] = test_set >> time_step & 1;
	    input[1] = !input[0];
	    input[2] = input[3] = 0;
	    set_input(input);
            apply_rule(training_data, data_index++); 
	    //cout << "\t" << input[0] << " " << input[1] << " " << input[2] << " " << input[3] << endl;
	}
	// Distractor period
	for (; time_step < distractor_end; ++time_step) {
	    input[0] = input[1] = input[3] = 0;
	    input[2] = 1;
	    set_input(input);
            apply_rule(training_data, data_index++); 
	}
	// Distractor signal
	input[0] = input[1] = input[2] = 0;
	input[3] = 1;
	set_input(input);
	apply_rule(training_data, data_index++); 
	++time_step;
        // Recall period
	for (; time_step < SEQUENCE_LENGTH; ++time_step) {
	    input[0] = input[1] = input[3] = 0;
	    input[2] = 1;
	    set_input(input);
            apply_rule(training_data, data_index++); 
	}
    }
    //cout << "Data_index: " << data_index << endl;
}

/***************************************************************************************/

int CA::mod(int x, int y) {
    try {
	if (y < 0)
	    throw NegativeModulusException();
    }
    catch(NegativeModulusException e) {
	cout << "Error: b must be non-negative in a mod b.\n"; 
	exit(1);
    }
    int r = x % y;
    return r < 0 ? r + y : r;
}


/***************************************************************************************/

int CA::base_N_to_dec(int num[], int base, int length) {
    int total = 0;
    int place = 1;
    
    for (int i = length - 1; i >= 0; --i) {
	total += place * num[i];
        place *= base;
    }
    return total;
}






