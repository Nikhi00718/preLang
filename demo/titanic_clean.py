# A typical Titanic cleaning script, written the way most notebooks do it.
# frontend/pandas2prep.py lifts its cleaning into PrepLang (the demo, step 7).
import pandas as pd

train = pd.read_csv("train.csv")
print(train.head())

train["Age"] = train["Age"].fillna(train["Age"].median())
train["Embarked"] = train["Embarked"].fillna(train["Embarked"].mode()[0])
train.drop(["Cabin", "Ticket"], axis=1, inplace=True)
train = train[train["Fare"] < 500]
assert train["Age"].between(0, 100).all()

# Feature engineering: this is where PrepLang stops lifting.
train["Sex"] = train["Sex"].map({"male": 0, "female": 1})
train.to_csv("train_clean.csv", index=False)
